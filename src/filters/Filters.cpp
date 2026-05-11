// filters/Filters.cpp

#include "Filters.hpp"
#include "Triangulate.hpp"
#include "detail.hpp"

#include <algorithm>
#include <cmath>

namespace pelpaint::filters {

using namespace detail;


FilterResult quantise_to_palette(
    std::span<const Pixel> src,
    std::span<const Pixel> palette)
{
    if (palette.empty()) return std::unexpected(Error::EmptyPalette());

    std::vector<Pixel> out;
    try { out.reserve(src.size()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (const auto& p : src)
        out.push_back(find_nearest(p, palette));

    return out;
}


FilterResult pixelify(
    std::span<const Pixel> src,
    int w, int h,
    int blockSize,
    std::span<const Pixel> palette)
{
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());
    if (blockSize <= 0)                   return std::unexpected(Error::InvalidGridSz());

    std::vector<Pixel> out;
    try { out.assign(src.begin(), src.end()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (int by = 0; by < h; by += blockSize) {
        for (int bx = 0; bx < w; bx += blockSize) {
            const int maxX = std::min(bx + blockSize, w);
            const int maxY = std::min(by + blockSize, h);
            int sumR = 0, sumG = 0, sumB = 0, sumA = 0, count = 0;
            for (int y = by; y < maxY; ++y)
                for (int x = bx; x < maxX; ++x) {
                    const Pixel& p = src[pix_idx(x, y, w)];
                    sumR += p.r; sumG += p.g; sumB += p.b; sumA += p.a; ++count;
                }
            Pixel avg{
                clamp8(sumR / count),
                clamp8(sumG / count),
                clamp8(sumB / count),
                clamp8(sumA / count),
            };
            if (!palette.empty()) avg = find_nearest(avg, palette);
            for (int y = by; y < maxY; ++y)
                for (int x = bx; x < maxX; ++x)
                    out[pix_idx(x, y, w)] = avg;
        }
    }
    return out;
}


FilterResult blur(
    std::span<const Pixel> src,
    int w, int h,
    int radius,
    bool gaussian)
{
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());
    if (radius <= 0)                      return std::unexpected(Error::InvalidDims());

    std::vector<Pixel> a, b;
    try {
        a.assign(src.begin(), src.end());
        b.resize(src.size());
    } catch (...) { return std::unexpected(Error::AllocFailed()); }

    const int passes = gaussian ? 3 : 1;
    for (int pass = 0; pass < passes; ++pass) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int R = 0, G = 0, B = 0, A = 0, cnt = 0;
                for (int ky = -radius; ky <= radius; ++ky)
                    for (int kx = -radius; kx <= radius; ++kx) {
                        const int nx = x + kx;
                        const int ny = y + ky;
                        if (!in_bounds(nx, ny, w, h)) continue;
                        const auto& p = a[pix_idx(nx, ny, w)];
                        R += p.r; G += p.g; B += p.b; A += p.a; ++cnt;
                    }
                b[pix_idx(x, y, w)] = {
                    clamp8(R / cnt), clamp8(G / cnt),
                    clamp8(B / cnt), clamp8(A / cnt),
                };
            }
        }
        std::swap(a, b);
    }
    return a;
}



FilterResult sharpen(
    std::span<const Pixel> src,
    int w, int h,
    float strength)
{
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());

    constexpr int K[3][3] = {
        { 0,-1, 0}, {-1, 4,-1}, { 0,-1, 0}
    };

    std::vector<Pixel> out;
    try { out.reserve(src.size()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int R = 0, G = 0, B = 0;
            for (int ky = -1; ky <= 1; ++ky)
                for (int kx = -1; kx <= 1; ++kx) {
                    const auto& p = src[pix_idx(
                        std::clamp(x + kx, 0, w-1),
                        std::clamp(y + ky, 0, h-1), w)];
                    const int k = K[ky+1][kx+1];
                    R += p.r * k; G += p.g * k; B += p.b * k;
                }
            const auto& orig = src[pix_idx(x, y, w)];
            out.push_back({
                clamp8(static_cast<int>(orig.r + strength * R)),
                clamp8(static_cast<int>(orig.g + strength * G)),
                clamp8(static_cast<int>(orig.b + strength * B)),
                orig.a,
            });
        }
    }
    return out;
}


FilterResult edge_detect(
    std::span<const Pixel> src,
    int w, int h,
    EdgeDetectMode mode,
    float threshold,
    bool invert_output)
{
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());

    auto lum = [](const Pixel& p) noexcept {
        return 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
    };

    std::vector<float> mag(static_cast<size_t>(w * h), 0.f);
    float maxMag = 1.f;

    for (int y = 1; y < h-1; ++y) {
        for (int x = 1; x < w-1; ++x) {
            const float tl = lum(src[pix_idx(x-1,y-1,w)]);
            const float tc = lum(src[pix_idx(x,  y-1,w)]);
            const float tr = lum(src[pix_idx(x+1,y-1,w)]);
            const float ml = lum(src[pix_idx(x-1,y,  w)]);
            const float mr = lum(src[pix_idx(x+1,y,  w)]);
            const float bl = lum(src[pix_idx(x-1,y+1,w)]);
            const float bc = lum(src[pix_idx(x,  y+1,w)]);
            const float br = lum(src[pix_idx(x+1,y+1,w)]);

            float m = 0.f;
            if (mode == EdgeDetectMode::Sobel) {
                const float gx = -tl + tr - 2*ml + 2*mr - bl + br;
                const float gy = -tl - 2*tc - tr + bl + 2*bc + br;
                m = std::sqrt(gx*gx + gy*gy);
            } else {
                // Laplacian
                m = std::abs(-tl - tc - tr - ml + 8*lum(src[pix_idx(x,y,w)]) - mr - bl - bc - br);
            }
            mag[pix_idx(x, y, w)] = m;
            maxMag = std::max(maxMag, m);
        }
    }

    std::vector<Pixel> out;
    try { out.resize(src.size(), {0, 0, 0, 0}); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    const float scale = 255.f / maxMag;
    const auto  thr   = static_cast<uint8_t>(threshold);
    for (int i = 0; i < w * h; ++i) {
        uint8_t v = clamp8(static_cast<int>(mag[i] * scale));
        if (v < thr) v = 0;
        if (invert_output) v = 255 - v;
        out[i] = {v, v, v, src[i].a};
    }
    return out;
}

FilterResult outline_layer(
    std::span<const Pixel> src,
    int w, int h,
    const OutlineConfig& cfg)
{
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());

    const int N       = w * h;
    const int minDist = 1;
    const int maxDist = cfg.pen_size + 1;

    std::vector<int> distField(N, INT_MAX);
    std::vector<int> queue;
    queue.reserve(N);

    // Seed: opaque pixels are "inside" at distance 0.
    for (int i = 0; i < N; ++i)
        if (src[i].a >= cfg.alpha_threshold) {
            distField[i] = 0;
            queue.push_back(i);
        }

    // BFS to fill distance field.
    for (int qi = 0; qi < static_cast<int>(queue.size()); ++qi) {
        const int idx = queue[qi];
        const int d   = distField[idx];
        if (d >= maxDist) continue;
        const int x = idx % w;
        const int y = idx / w;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx   = x + dx;
                const int ny   = y + dy;
                if (!in_bounds(nx, ny, w, h)) continue;
                const int nidx = pix_idx(nx, ny, w);
                if (distField[nidx] == INT_MAX) {
                    distField[nidx] = d + 1;
                    queue.push_back(nidx);
                }
            }
    }

    // Build ring colour (optionally lightened).
    Pixel ring = cfg.color;
    if (cfg.auto_lighten) {
        const float f = cfg.lighten_factor;
        ring.r = clamp8(ring.r + static_cast<int>((255 - ring.r) * f));
        ring.g = clamp8(ring.g + static_cast<int>((255 - ring.g) * f));
        ring.b = clamp8(ring.b + static_cast<int>((255 - ring.b) * f));
    }

    std::vector<Pixel> out(N, {0, 0, 0, 0});
    const float fadeRange = static_cast<float>(cfg.pen_size);

    for (int i = 0; i < N; ++i) {
        const int d = distField[i];
        if (d < minDist || d >= maxDist) continue;  // inside or outside ring

        uint8_t alpha = ring.a;
        if (cfg.edge_mode == OutlineEdge::Opacity) {
            const float fade = 1.f - (static_cast<float>(d - minDist) / fadeRange);
            alpha = static_cast<uint8_t>(ring.a * fade);
        }
        out[i] = {ring.r, ring.g, ring.b, alpha};

        // Rim mode: blend away the interior-facing edge (d==minDist).
        if (cfg.mode == OutlineMode::Rim && d == minDist)
            out[i].a = static_cast<uint8_t>(alpha / 2);
    }
    return out;
}



FilterResult triangulate(
    std::span<const Pixel>    src,
    int                       w,
    int                       h,
    const TriangulateOptions& opts)
{
    try {
        return TriangulateImage(src, w, h, opts);
    } catch (...) {
        return std::unexpected(Error::AllocFailed());
    }
}

} // namespace pelpaint::filters
