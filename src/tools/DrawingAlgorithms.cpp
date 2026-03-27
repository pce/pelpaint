#include "DrawingAlgorithms.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>
#include <vector>

namespace pelpaint::tools {

void BlendPixel(Pixel& dst, const Pixel& src) noexcept
{
    if (src.a == 0) {
        dst = src;          // erase to transparent
    } else if (src.a == 255) {
        dst = src;          // fully opaque — simple overwrite
    } else {
        const float alpha = src.a / 255.0f;
        const float inv   = 1.0f - alpha;
        dst.r = static_cast<uint8_t>(src.r * alpha + dst.r * inv);
        dst.g = static_cast<uint8_t>(src.g * alpha + dst.g * inv);
        dst.b = static_cast<uint8_t>(src.b * alpha + dst.b * inv);
        dst.a = static_cast<uint8_t>(
            std::min(255, static_cast<int>(dst.a) + static_cast<int>(src.a)));
    }
}

// ============================================================
// Internal: write one pixel directly into the layer span.
// Performs bounds check, selection check, then BlendPixel.
// Marks the canvas dirty so the per-frame composite fires.
// ============================================================

static void WritePixel(DrawCtx& ctx,
                        std::span<Pixel> span,
                        int x, int y,
                        const Pixel& color) noexcept
{
    if (!ctx.canvas.IsValidCoord(x, y)) return;
    if (ctx.inSelection && !ctx.inSelection(x, y)) return;

    const std::size_t idx = static_cast<std::size_t>(
        ctx.canvas.PixelIndex(x, y));
    BlendPixel(span[idx], color);
    ctx.canvas.SetDirty();
}

// ============================================================
// DrawCircleFilled / DrawCircleOutline
// ============================================================

void DrawCircleFilled(DrawCtx& ctx,
                       int cx, int cy, int radius,
                       const Pixel& color)
{
    Layer* layer = ctx.canvas.ActiveLayer();
    if (!layer || layer->locked) return;

    auto span = ctx.canvas.ActiveLayerSpan();
    if (span.empty()) return;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) {
                WritePixel(ctx, span, cx + dx, cy + dy, color);
            }
        }
    }
}

void DrawCircleOutline(DrawCtx& ctx,
                        int cx, int cy, int radius,
                        const Pixel& color)
{
    Layer* layer = ctx.canvas.ActiveLayer();
    if (!layer || layer->locked) return;

    auto span = ctx.canvas.ActiveLayerSpan();
    if (span.empty()) return;

    // Midpoint circle algorithm (Bresenham variant)
    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;

    auto plot8 = [&](int px, int py) {
        WritePixel(ctx, span, cx + px, cy + py, color);
        WritePixel(ctx, span, cx - px, cy + py, color);
        WritePixel(ctx, span, cx + px, cy - py, color);
        WritePixel(ctx, span, cx - px, cy - py, color);
        WritePixel(ctx, span, cx + py, cy + px, color);
        WritePixel(ctx, span, cx - py, cy + px, color);
        WritePixel(ctx, span, cx + py, cy - px, color);
        WritePixel(ctx, span, cx - py, cy - px, color);
    };

    while (x <= y) {
        plot8(x, y);
        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            --y;
        }
        ++x;
    }
}

// ============================================================
// DrawLineBresenham
// ============================================================

void DrawLineBresenham(DrawCtx& ctx,
                        int x0, int y0,
                        int x1, int y1,
                        const Pixel& color,
                        float brushSize)
{
    Layer* layer = ctx.canvas.ActiveLayer();
    if (!layer || layer->locked) return;

    const int radius = static_cast<int>(brushSize * 0.5f);

    const int dx =  std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int sy = (y0 < y1) ? 1 : -1;
    int       err = dx + dy;

    while (true) {
        DrawCircleFilled(ctx, x0, y0, radius, color);

        if (x0 == x1 && y0 == y1) break;

        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// ============================================================
// FloodFill
// ============================================================

void FloodFill(DrawCtx& ctx,
               int x, int y,
               const Pixel& fillColor)
{
    if (!ctx.canvas.IsValidCoord(x, y)) return;

    Layer* layer = ctx.canvas.ActiveLayer();
    if (!layer || layer->locked) return;

    const Pixel targetColor = ctx.canvas.GetPixel(x, y);

    // Nothing to do if target already matches fill colour.
    if (targetColor.r == fillColor.r &&
        targetColor.g == fillColor.g &&
        targetColor.b == fillColor.b &&
        targetColor.a == fillColor.a) return;

    const int W = ctx.canvas.Width();
    const int H = ctx.canvas.Height();
    auto      span = ctx.canvas.ActiveLayerSpan();

    std::vector<bool> visited(static_cast<std::size_t>(W) * H, false);
    std::queue<std::pair<int,int>> queue;

    queue.push({x, y});
    visited[static_cast<std::size_t>(ctx.canvas.PixelIndex(x, y))] = true;

    while (!queue.empty()) {
        auto [cx, cy] = queue.front();
        queue.pop();

        if (!ctx.canvas.IsValidCoord(cx, cy)) continue;
        if (ctx.inSelection && !ctx.inSelection(cx, cy)) continue;

        // Only fill pixels that still match the target colour.
        const std::size_t idx = static_cast<std::size_t>(
            ctx.canvas.PixelIndex(cx, cy));
        const Pixel& cur = span[idx];
        if (cur.r != targetColor.r || cur.g != targetColor.g ||
            cur.b != targetColor.b || cur.a != targetColor.a) continue;

        BlendPixel(span[idx], fillColor);
        ctx.canvas.SetDirty();

        // Enqueue 4-connected neighbours.
        constexpr int nx[4] = { 1, -1,  0,  0 };
        constexpr int ny[4] = { 0,  0,  1, -1 };
        for (int i = 0; i < 4; ++i) {
            const int qx = cx + nx[i];
            const int qy = cy + ny[i];
            if (!ctx.canvas.IsValidCoord(qx, qy)) continue;
            const std::size_t ni =
                static_cast<std::size_t>(ctx.canvas.PixelIndex(qx, qy));
            if (!visited[ni]) {
                visited[ni] = true;
                queue.push({qx, qy});
            }
        }
    }
}

// ============================================================
// FloodFillThreshold
// ============================================================

void FloodFillThreshold(DrawCtx& ctx,
                         int x, int y,
                         const Pixel& fillColor,
                         float threshold)
{
    if (!ctx.canvas.IsValidCoord(x, y)) return;

    Layer* layer = ctx.canvas.ActiveLayer();
    if (!layer || layer->locked) return;

    const Pixel targetColor = ctx.canvas.GetPixel(x, y);

    const int W = ctx.canvas.Width();
    const int H = ctx.canvas.Height();
    auto      span = ctx.canvas.ActiveLayerSpan();

    std::vector<bool> visited(static_cast<std::size_t>(W) * H, false);
    std::queue<std::pair<int,int>> queue;

    queue.push({x, y});
    visited[static_cast<std::size_t>(ctx.canvas.PixelIndex(x, y))] = true;

    while (!queue.empty()) {
        auto [cx, cy] = queue.front();
        queue.pop();

        if (!ctx.canvas.IsValidCoord(cx, cy)) continue;
        if (ctx.inSelection && !ctx.inSelection(cx, cy)) continue;

        const std::size_t idx = static_cast<std::size_t>(
            ctx.canvas.PixelIndex(cx, cy));
        BlendPixel(span[idx], fillColor);
        ctx.canvas.SetDirty();

        constexpr int nx[4] = { 1, -1,  0,  0 };
        constexpr int ny[4] = { 0,  0,  1, -1 };
        for (int i = 0; i < 4; ++i) {
            const int qx = cx + nx[i];
            const int qy = cy + ny[i];
            if (!ctx.canvas.IsValidCoord(qx, qy)) continue;
            const std::size_t ni =
                static_cast<std::size_t>(ctx.canvas.PixelIndex(qx, qy));
            if (visited[ni]) continue;

            // Enqueue only if neighbour is within threshold of the target colour.
            const Pixel& neighbour = span[ni];
            if (ColorDistance(targetColor, neighbour) <= threshold) {
                visited[ni] = true;
                queue.push({qx, qy});
            }
        }
    }
}

// ============================================================
// DrawSpray
// ============================================================

void DrawSpray(DrawCtx& ctx,
               int x, int y,
               float radius,
               const Pixel& color,
               float density)
{
    Layer* layer = ctx.canvas.ActiveLayer();
    if (!layer || layer->locked) return;

    auto span = ctx.canvas.ActiveLayerSpan();
    if (span.empty()) return;

    const int numDots = static_cast<int>(radius * radius * density);

    for (int i = 0; i < numDots; ++i) {
        const float angle = static_cast<float>(std::rand()) /
                            static_cast<float>(RAND_MAX) * 6.28318f;
        const float dist  = static_cast<float>(std::rand()) /
                            static_cast<float>(RAND_MAX) * radius;

        const int px = x + static_cast<int>(std::cos(angle) * dist);
        const int py = y + static_cast<int>(std::sin(angle) * dist);

        WritePixel(ctx, span, px, py, color);
    }
}

// ============================================================
// DrawPenStroke  (BrushMode::Pen — calligraphic nib)
// ============================================================

void DrawPenStroke(DrawCtx& ctx,
                   int x0, int y0,
                   int x1, int y1,
                   const Pixel& color,
                   float brushSize,
                   float pressure,
                   float tiltX,
                   float tiltY)
{
    Layer* layer = ctx.canvas.ActiveLayer();
    if (!layer || layer->locked) return;

    auto span = ctx.canvas.ActiveLayerSpan();
    if (span.empty()) return;

    // ------------------------------------------------------------------
    // Nib geometry derived from pressure and tilt.
    //
    // tiltMag : 0 = pen upright (circular nib), 1 = fully tilted (flat).
    // nibAngle: direction the pen leans — the flat edge of the nib is
    //           perpendicular to this direction.
    // majorR  : half-width across the flat (wide) edge of the nib.
    // minorR  : half-height along the tilt direction (shrinks with tilt).
    //
    // The result is a rotated ellipse that produces the characteristic
    // thick-and-thin variation of a real calligraphic pen nib.
    // ------------------------------------------------------------------
    const float safePressure = std::max(0.05f, pressure);
    const float tiltMag      = std::min(1.0f, std::sqrt(tiltX * tiltX + tiltY * tiltY));
    // nibAngle: direction the pen leans; nib major axis is perpendicular.
    const float nibAngle     = std::atan2(tiltY, tiltX);

    // Pressure scales the nib size (hard press → wide stroke, like SDL example).
    const float majorR = std::max(0.5f, brushSize * 0.5f * safePressure);
    // Tilt flattens the nib: at full tilt minorR ≈ 25 % of majorR.
    const float minorR = std::max(0.5f, majorR * (1.0f - tiltMag * 0.75f));

    // Pre-compute rotation for the nib ellipse (rotate into nib-local space).
    const float cosA = std::cos(nibAngle);
    const float sinA = std::sin(nibAngle);

    // Alpha modulated by pressure — lighter press → more transparent stroke.
    Pixel stampColor = color;
    stampColor.a = static_cast<uint8_t>(
        std::clamp(static_cast<float>(color.a) * safePressure, 0.0f, 255.0f));

    // Bounding-box half-extent for the rotated ellipse pixel search.
    const int bbox = static_cast<int>(majorR) + 1;

    // Stamp a rotated nib ellipse centred at canvas pixel (px, py).
    auto stampNib = [&](int px, int py) {
        for (int ky = -bbox; ky <= bbox; ++ky) {
            for (int kx = -bbox; kx <= bbox; ++kx) {
                // Rotate offset (kx, ky) into nib-local coordinates.
                const float rx =  cosA * static_cast<float>(kx)
                                + sinA * static_cast<float>(ky);
                const float ry = -sinA * static_cast<float>(kx)
                                + cosA * static_cast<float>(ky);
                // Ellipse test: (rx/majorR)² + (ry/minorR)² ≤ 1
                const float ex = rx / majorR;
                const float ey = ry / minorR;
                if (ex * ex + ey * ey <= 1.0f) {
                    WritePixel(ctx, span, px + kx, py + ky, stampColor);
                }
            }
        }
    };

    // Bresenham walk from (x0,y0) to (x1,y1); stamp the nib at every step
    // so the stroke is continuous even at low frame rates.
    const int dx =  std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int sy = (y0 < y1) ? 1 : -1;
    int       err = dx + dy;
    int       wx  = x0;
    int       wy  = y0;

    while (true) {
        stampNib(wx, wy);
        if (wx == x1 && wy == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; wx += sx; }
        if (e2 <= dx) { err += dx; wy += sy; }
    }
}

// ============================================================
// DrawPixelBrush  (BrushMode::PixelBrush — watercolor scatter)
// ============================================================

void DrawPixelBrush(DrawCtx& ctx,
                    int cx, int cy,
                    float radius,
                    const Pixel& color,
                    float pressure,
                    float accumulation)
{
    Layer* layer = ctx.canvas.ActiveLayer();
    if (!layer || layer->locked) return;

    auto span = ctx.canvas.ActiveLayerSpan();
    if (span.empty()) return;

    const float safePressure = std::max(0.01f, pressure);

    // ------------------------------------------------------------------
    // 1. Solid core circle — tight, pressure-scaled radius.
    //    This is the initial ink deposit, always present.
    // ------------------------------------------------------------------
    const float coreRadius = std::max(0.5f, radius * safePressure * 0.35f);
    const int   coreR      = static_cast<int>(coreRadius);

    for (int dy2 = -coreR; dy2 <= coreR; ++dy2) {
        for (int dx2 = -coreR; dx2 <= coreR; ++dx2) {
            if (dx2 * dx2 + dy2 * dy2 <= coreR * coreR) {
                Pixel c  = color;
                c.a = static_cast<uint8_t>(
                    std::clamp(static_cast<float>(color.a) * safePressure,
                               0.0f, 255.0f));
                WritePixel(ctx, span, cx + dx2, cy + dy2, c);
            }
        }
    }

    // ------------------------------------------------------------------
    // 2. Polar raymarching scatter — wet-ink / watercolor tendrils.
    //
    //  • Cast kRays radial rays from (cx, cy).
    //  • Each ray carries a small angular wobble for an organic silhouette.
    //  • March outward from coreRadius to maxDist along each ray, placing
    //    pixels with probability that decays quadratically with distance
    //    (dense near the core, sparse at the fringe).
    //  • accumulation grows while the pen is held, extending maxDist to
    //    simulate ink soaking into the paper over time.
    //  • Pixels in the outer ~20 % of the reach receive wet-edge darkening
    //    (pigment concentrates at the drying boundary in real watercolour).
    // ------------------------------------------------------------------
    constexpr int kRays  = 24;
    // maxDist grows with accumulation; pressure limits the overall spread.
    const float maxDist  = radius * (1.0f + accumulation * 0.55f) * safePressure;
    const float minDist  = coreRadius * 0.8f;

    if (maxDist <= minDist) return;   // nothing to scatter yet

    for (int ray = 0; ray < kRays; ++ray) {
        // Per-ray angular wobble for organic irregularity.
        const float wobble = (static_cast<float>(std::rand()) /
                              static_cast<float>(RAND_MAX) - 0.5f) * 0.35f;
        const float angle  = ray * (6.28318f / static_cast<float>(kRays)) + wobble;
        const float cosA   = std::cos(angle);
        const float sinA   = std::sin(angle);

        float dist = minDist;
        while (dist <= maxDist) {
            // t: normalised distance from core to outer edge (0→1).
            const float t = std::clamp(
                (dist - minDist) / (maxDist - minDist), 0.0f, 1.0f);

            // Probability of placing a pixel: dense near core, sparse at fringe.
            const float placeProbability = safePressure * (1.0f - t * t);
            const float roll = static_cast<float>(std::rand()) /
                               static_cast<float>(RAND_MAX);

            if (roll < placeProbability) {
                const int px = cx + static_cast<int>(cosA * dist);
                const int py = cy + static_cast<int>(sinA * dist);

                // Alpha falls off with distance; wet-edge boost near the fringe.
                const float edgeFactor = (t > 0.78f) ? 1.25f : 1.0f;
                const float alpha = std::clamp(
                    static_cast<float>(color.a) * safePressure *
                    (1.0f - t) * edgeFactor,
                    0.0f, 255.0f);

                Pixel c  = color;
                c.a = static_cast<uint8_t>(alpha);

                // Wet-edge darkening: pigment concentrates at the drying boundary.
                if (t > 0.75f) {
                    constexpr float kDarken = 0.82f;
                    c.r = static_cast<uint8_t>(static_cast<float>(c.r) * kDarken);
                    c.g = static_cast<uint8_t>(static_cast<float>(c.g) * kDarken);
                    c.b = static_cast<uint8_t>(static_cast<float>(c.b) * kDarken);
                }

                WritePixel(ctx, span, px, py, c);
            }

            // Step size: finer near the core, coarser near the edge.
            // This naturally produces higher pixel density at the core and
            // a sparser, more transparent fringe.
            dist += 0.5f + t * 1.2f;
        }
    }
}

// ============================================================
// ColorDistance
// ============================================================

float ColorDistance(const Pixel& a, const Pixel& b) noexcept
{
    const int dr = static_cast<int>(a.r) - static_cast<int>(b.r);
    const int dg = static_cast<int>(a.g) - static_cast<int>(b.g);
    const int db = static_cast<int>(a.b) - static_cast<int>(b.b);
    const int da = static_cast<int>(a.a) - static_cast<int>(b.a);
    return std::sqrt(static_cast<float>(dr*dr + dg*dg + db*db + da*da));
}

} // namespace pelpaint::tools
