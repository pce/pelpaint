#include "PixelPerfectGenerator.hpp"
#include <cmath>
#include <algorithm>
#include <queue>
#include <stack>

namespace pelpaint::tools {

std::string PixelPerfectGenerator::PixelPerfect::ToAsciiArt() const {
    std::string result;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Pixel pixel = pixels[y * width + x];
            // Find closest palette color
            char bestChar = '.';
            int minDiff = 255 * 3;

            for (const auto& [ch, col] : palette) {
                int diff = std::abs(pixel.r - col.r) +
                          std::abs(pixel.g - col.g) +
                          std::abs(pixel.b - col.b);
                if (diff < minDiff) {
                    minDiff = diff;
                    bestChar = ch;
                }
            }
            result += bestChar;
        }
        result += '\n';
    }
    return result;
}

PixelPerfectGenerator::PixelPerfect PixelPerfectGenerator::GenerateLSystem(const LSysConfig& config, int width, int height) {
    // Generate L-System string
    std::string current = config.axiom;
    for (int i = 0; i < config.iterations; ++i) {
        std::string next;
        for (char c : current) {
            auto it = config.rules.find(c);
            if (it != config.rules.end()) {
                next += it->second;
            } else {
                next += c;
            }
        }
        current = next;
    }

    // Create pixel art
    PixelPerfect art;
    art.width = width;
    art.height = height;
    art.pixels.resize(width * height, {0, 0, 0, 0});
    art.indices.resize(width * height, '.');

    // Initialize drawing state
    struct State {
        float x, y;
        float angle;
    };
    std::stack<State> states;
    float x = width / 2.0f;
    float y = height - 10.0f;
    float angle = -90.0f;
    float length = 5.0f;

    // Process L-System string
    for (char c : current) {
        switch (c) {
            case 'F':
                if (auto it = config.colors.find("branch"); it != config.colors.end()) {
                    DrawLSystemBranch(art.pixels, width, height, x, y, angle, length, it->second);
                }
                x += length * std::cos(angle * M_PI / 180.0f);
                y += length * std::sin(angle * M_PI / 180.0f);
                break;
            case '+':
                angle += config.angle;
                break;
            case '-':
                angle -= config.angle;
                break;
            case '[':
                states.push({x, y, angle});
                break;
            case ']':
                if (!states.empty()) {
                    auto state = states.top();
                    states.pop();
                    x = state.x;
                    y = state.y;
                    angle = state.angle;
                }
                break;
        }
    }

    return art;
}

void PixelPerfectGenerator::DrawLSystemBranch(std::vector<Pixel>& pixels, int width, int height,
                                        float x, float y, float angle, float length,
                                        const Pixel& color) {
    float endX = x + length * std::cos(angle * M_PI / 180.0f);
    float endY = y + length * std::sin(angle * M_PI / 180.0f);

    // Bresenham's line algorithm
    int x1 = static_cast<int>(x);
    int y1 = static_cast<int>(y);
    int x2 = static_cast<int>(endX);
    int y2 = static_cast<int>(endY);

    int dx = std::abs(x2 - x1);
    int dy = std::abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;

    while (true) {
        if (x1 >= 0 && x1 < width && y1 >= 0 && y1 < height) {
            pixels[y1 * width + x1] = color;
        }

        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

PixelPerfectGenerator::PixelPerfect PixelPerfectGenerator::GeneratePattern(const PatternConfig& config, int width, int height) {
    PixelPerfect art;
    art.width = width;
    art.height = height;
    art.pixels.resize(width * height, {0, 0, 0, 0});
    art.indices.resize(width * height, '.');
    art.indices.resize(width * height, '.');
    art.palette = config.colors;

    int patternHeight = config.pattern.size();
    int patternWidth = patternHeight > 0 ? config.pattern[0].length() : 0;

    if (patternWidth == 0 || patternHeight == 0) return art;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int py = config.repeat_y ? y % patternHeight : y;
            int px = config.repeat_x ? x % patternWidth : x;

            if (py < patternHeight && px < patternWidth) {
                char c = config.pattern[py][px];
                art.indices[y * width + x] = c;
                auto it = config.colors.find(c);
                if (it != config.colors.end()) {
                    art.pixels[y * width + x] = it->second;
                }
            }
        }
    }

    return art;
}

PixelPerfectGenerator::PixelPerfect PixelPerfectGenerator::GenerateCellular(const CellularConfig& config, int width, int height) {
    PixelPerfect art;
    art.width = width;
    art.height = height;
    art.pixels.resize(width * height);
    art.indices.resize(width * height);

    std::vector<bool> grid(width * height);
    auto& rng = GetRNG();
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // Initial fill
    for (int i = 0; i < width * height; ++i) {
        grid[i] = dist(rng) < config.initial_fill;
    }

    // Iterations
    for (int iter = 0; iter < config.iterations; ++iter) {
        std::vector<bool> nextGrid = grid;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int neighbors = CountNeighbors(grid, width, height, x, y);
                if (grid[y * width + x]) {
                    nextGrid[y * width + x] = neighbors >= config.survive_rule;
                } else {
                    nextGrid[y * width + x] = neighbors >= config.birth_rule;
                }
            }
        }
        grid = nextGrid;
    }

    // Map to pixels
    for (int i = 0; i < width * height; ++i) {
        art.pixels[i] = grid[i] ? config.alive_color : config.dead_color;
        art.indices[i] = grid[i] ? '1' : '0';
    }
    art.palette['1'] = config.alive_color;
    art.palette['0'] = config.dead_color;

    return art;
}

int PixelPerfectGenerator::CountNeighbors(const std::vector<bool>& grid, int width, int height, int x, int y) {
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                if (grid[ny * width + nx]) count++;
            }
        }
    }
    return count;
}

float PixelPerfectGenerator::Perlin2D(float x, float y, float scale) {
    x *= scale;
    y *= scale;

    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float sx = x - static_cast<float>(x0);
    float sy = y - static_cast<float>(y0);

    auto& rng = GetRNG();
    auto hash = [&](int x, int y) {
        rng.seed(x * 16777619 ^ y);
        return static_cast<float>(rng()) / rng.max();
    };

    float n0 = hash(x0, y0);
    float n1 = hash(x1, y0);
    float ix0 = n0 + sx * (n1 - n0);

    n0 = hash(x0, y1);
    n1 = hash(x1, y1);
    float ix1 = n0 + sx * (n1 - n0);

    return ix0 + sy * (ix1 - ix0);
}

PixelPerfectGenerator::PixelPerfect PixelPerfectGenerator::GenerateNoise(int width, int height, float scale,
                                                           const std::vector<Pixel>& gradient) {
    PixelPerfect art;
    art.width = width;
    art.height = height;
    art.pixels.resize(width * height, {0, 0, 0, 0});

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float noise = Perlin2D(static_cast<float>(x), static_cast<float>(y), scale);

            // Map noise value to gradient
            size_t index = static_cast<size_t>(noise * (gradient.size() - 1));
            art.pixels[y * width + x] = gradient[index];
        }
    }

    return art;
}

std::vector<PixelPerfectGenerator::PixelPerfect> PixelPerfectGenerator::GenerateAnimation(
    const PixelPerfect& base, const std::vector<std::string>& modifyPattern,
    const std::vector<std::pair<int, int>>& region) {

    std::vector<PixelPerfect> frames;
    frames.push_back(base);

    for (const auto& pattern : modifyPattern) {
        PixelPerfect frame = base;
        int rx = region[0].first;
        int ry = region[0].second;
        int rw = region[1].first - rx;
        int rh = region[1].second - ry;

        for (int y = 0; y < rh && y < static_cast<int>(pattern.length()); ++y) {
            for (int x = 0; x < rw && x < static_cast<int>(pattern.length()); ++x) {
                if (rx + x < frame.width && ry + y < frame.height) {
                    char c = pattern[y * rw + x];
                    auto it = base.palette.find(c);
                    if (it != base.palette.end()) {
                        frame.pixels[(ry + y) * frame.width + (rx + x)] = it->second;
                    }
                }
            }
        }
        frames.push_back(frame);
    }

    return frames;
}

PixelPerfectGenerator::PixelPerfect PixelPerfectGenerator::CyclePalette(const PixelPerfect& art, float time, float speed, const std::vector<char>& targetKeys) {
    if (targetKeys.size() < 2) return art;

    PixelPerfect result = art;
    float offset = time * speed;
    int n = targetKeys.size();

    for (size_t i = 0; i < result.pixels.size(); ++i) {
        char key = result.indices[i];

        // Find if this key is in the targets to cycle
        auto it = std::find(targetKeys.begin(), targetKeys.end(), key);
        if (it != targetKeys.end()) {
            int idx = std::distance(targetKeys.begin(), it);
            // Calculate new color by interpolating between palette entries
            float virtualIdx = fmod(static_cast<float>(idx) + offset, static_cast<float>(n));
            int i1 = static_cast<int>(floor(virtualIdx)) % n;
            int i2 = (i1 + 1) % n;
            float t = virtualIdx - floor(virtualIdx);

            Pixel c1 = art.palette.at(targetKeys[i1]);
            Pixel c2 = art.palette.at(targetKeys[i2]);

            result.pixels[i].r = static_cast<unsigned char>(c1.r + t * (static_cast<float>(c2.r) - c1.r));
            result.pixels[i].g = static_cast<unsigned char>(c1.g + t * (static_cast<float>(c2.g) - c1.g));
            result.pixels[i].b = static_cast<unsigned char>(c1.b + t * (static_cast<float>(c2.b) - c1.b));
            result.pixels[i].a = static_cast<unsigned char>(c1.a + t * (static_cast<float>(c2.a) - c1.a));
        }
    }

    return result;
}

/**
 * Circular Ripple Wave Effect
 *
 * Every pixel orbits in a small ellipse defined by the phase equation:
 *
 *   phase = time  +  pos * frequency
 *
 *   dx = sin(phase) * amplitude        — primary X orbit
 *   dy = cos(phase) * amplitude        — primary Y orbit  (90° behind sin → circular)
 *
 * sin and cos evaluated at the *same* phase angle always lie on a unit circle,
 * so each pixel moves in a circle of radius `amplitude` as time advances.
 * Because neighbouring pixels carry different spatial phases (pos * frequency),
 * they occupy different points on their orbits at any instant — producing a
 * ripple that churns through the image like a 2D water surface.
 *
 * A second harmonic is layered on top of the primary orbit:
 *
 *   flutter = cos(phase * 2.0 + time) * amplitude * 0.30
 *
 * Running at twice the spatial frequency, the flutter wave beats against the
 * fundamental, and the interference between the two frequencies is what the
 * eye reads as the "pulsating" rhythm the effect is named for.
 *
 * The `horizontal` flag orients the dominant displacement axis: when true the
 * flutter term drives the horizontal shift and the full orbit drives the
 * vertical, so the ripple reads as primarily sideways; when false the
 * assignments are reversed and the ripple reads as primarily vertical.
 */
PixelPerfectGenerator::PixelPerfect PixelPerfectGenerator::ApplyPulseEffect(const PixelPerfect& art, float time, float amplitude, float frequency, bool horizontal) {
    PixelPerfect result = art;
    std::fill(result.pixels.begin(), result.pixels.end(), Pixel{0, 0, 0, 0});

    for (int y = 0; y < art.height; ++y) {
        for (int x = 0; x < art.width; ++x) {
            // Spatial phase: use the axis that defines the wave front.
            float pos   = static_cast<float>(horizontal ? y : x);
            float phase = time + pos * frequency;

            // circular orbit
            // sin → X displacement,  cos → Y displacement.
            // Together they place each pixel on a circle of radius `amplitude`
            // that rotates forward in time.
            float dx = sinf(phase) * amplitude;
            float dy = cosf(phase) * amplitude;

            // 2nd-harmonic flutter
            // cos at 2× spatial frequency creates a faster sub-wave that
            // beats against the fundamental, giving the pulsating rhythm.
            float flutter = cosf(phase * 2.0f + time) * amplitude * 0.30f;
            dx += flutter;
            dy += flutter;

            // Weight by dominant axis so the motion reads as the intended
            // direction while still displacing both axes for the 2D ripple.
            int sx = x + static_cast<int>(horizontal ? flutter : dx);
            int sy = y + static_cast<int>(horizontal ? dy      : flutter);

            if (sx >= 0 && sx < art.width && sy >= 0 && sy < art.height) {
                result.pixels[y * art.width + x]  = art.pixels[sy * art.width + sx];
                result.indices[y * art.width + x] = art.indices[sy * art.width + sx];
            }
        }
    }

    return result;
}


/**
 * Lissajous Warp Effect
 *
 * Jules Antoine Lissajous (1857) discovered that when two independent
 * sinusoidal oscillations govern the X and Y axes of a moving point,
 * the resulting path traces a characteristic knot whose shape depends
 * entirely on the *ratio* of the two frequencies:
 *
 *   x(t) = A · sin( a·t + δ )      — first oscillator
 *   y(t) = B · sin( b·t )          — second, unrelated oscillator
 *
 * Classic ratios and the curves they produce:
 *
 *   a:b = 1:1, δ = 0     →  diagonal line
 *   a:b = 1:1, δ = π/2   →  circle  (see ApplyPulseEffect)
 *   a:b = 2:1            →  figure-eight / parabola
 *   a:b = 3:2            →  bow-tie knot  ← what we use here
 *
 * Applied to pixels, each source coordinate (x, y) becomes the spatial
 * phase input to its own oscillator.  The X-axis oscillator runs at the
 * base `frequency`; the Y-axis oscillator runs at 3/2 of that frequency
 * with a π/2 phase offset so the two waves start in quadrature.  As
 * `time` advances, every pixel is pulled along its own Lissajous path,
 * and the image surface warps into the bow-tie knot topology.
 *
 * The `horizontal` flag orients the dominant warp axis: when true the
 * stronger displacement is horizontal (the Y oscillator drives rows);
 * when false the warp reads as vertical (the X oscillator drives columns).
 */
PixelPerfectGenerator::PixelPerfect PixelPerfectGenerator::ApplyOrbitalEffect(const PixelPerfect& art, float time, float amplitude, float frequency, bool horizontal) {
    PixelPerfect result = art;
    std::fill(result.pixels.begin(), result.pixels.end(), Pixel{0, 0, 0, 0});

    /**
     * Frequency ratio 3:2 — the bow-tie Lissajous knot.
     *
     * The Y oscillator runs 1.5× faster than the X oscillator.
     * A π/2 phase offset (added to the Y phase below) puts the two
     * waves in quadrature at t=0, which keeps the figure open and
     * prevents it collapsing to a straight line when time = 0.
     */
    const float ratioA = 1.0f;           /* X oscillator frequency multiplier */
    const float ratioB = 1.5f;           /* Y oscillator frequency multiplier  */
    const float delta  = 1.5707963f;     /* π/2 quadrature offset              */

    for (int y = 0; y < art.height; ++y) {
        for (int x = 0; x < art.width; ++x) {

            /**
             * Each axis has its own spatial phase, built from the pixel's
             * position along that axis scaled by `frequency`.  This means
             * neighbouring pixels sit at slightly different points on their
             * Lissajous paths, spreading the knot shape across the image.
             */
            float phaseX = time * ratioA + static_cast<float>(x) * frequency;
            float phaseY = time * ratioB + static_cast<float>(y) * frequency + delta;

            /**
             * The `horizontal` flag orients the dominant warp axis by swapping
             * which oscillator drives which spatial dimension.  When true, the
             * slower fundamental (ratioA) pulls columns left and right while the
             * faster harmonic (ratioB) pushes rows up and down — the image warps
             * primarily sideways.  When false the assignments are reversed and
             * the warp reads as primarily vertical.
             */
            int sx = x + static_cast<int>(sinf(horizontal ? phaseX : phaseY) * amplitude);
            int sy = y + static_cast<int>(sinf(horizontal ? phaseY : phaseX) * amplitude);

            if (sx >= 0 && sx < art.width && sy >= 0 && sy < art.height) {
                result.pixels[y * art.width + x] = art.pixels[sy * art.width + sx];
                result.indices[y * art.width + x] = art.indices[sy * art.width + sx];
            }
        }
    }

    return result;
}

PixelPerfectGenerator::PixelPerfect PixelPerfectGenerator::ApplySineWaveEffect(const PixelPerfect& art, float time, float amplitude, float frequency, bool horizontal) {
    PixelPerfect result = art;
    // Fill with transparent first
    std::fill(result.pixels.begin(), result.pixels.end(), Pixel{0, 0, 0, 0});

    for (int y = 0; y < art.height; ++y) {
        for (int x = 0; x < art.width; ++x) {
            float shift = sin(time + (horizontal ? y : x) * frequency) * amplitude;
            int sx = x + (horizontal ? static_cast<int>(shift) : 0);
            int sy = y + (horizontal ? 0 : static_cast<int>(shift));

            if (sx >= 0 && sx < art.width && sy >= 0 && sy < art.height) {
                result.pixels[y * art.width + x] = art.pixels[sy * art.width + sx];
                result.indices[y * art.width + x] = art.indices[sy * art.width + sx];
            }
        }
    }

    return result;
}

PixelPerfectGenerator::PixelPerfect PixelPerfectGenerator::CreateTiled(const PixelPerfect& art, int multiplier_x, int multiplier_y) {
    PixelPerfect result;
    result.width = art.width * multiplier_x;
    result.height = art.height * multiplier_y;
    result.palette = art.palette;
    result.pixels.resize(result.width * result.height);
    result.indices.resize(result.width * result.height);

    for (int ty = 0; ty < multiplier_y; ++ty) {
        for (int tx = 0; tx < multiplier_x; ++tx) {
            for (int y = 0; y < art.height; ++y) {
                for (int x = 0; x < art.width; ++x) {
                    int targetX = tx * art.width + x;
                    int targetY = ty * art.height + y;
                    result.pixels[targetY * result.width + targetX] = art.pixels[y * art.width + x];
                    result.indices[targetY * result.width + targetX] = art.indices[y * art.width + x];
                }
            }
        }
    }

    return result;
}

} // namespace pelpaint::tools
