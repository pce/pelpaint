#include "Filters.hpp"
#include "Triangulate.hpp"

#include <algorithm>
#include <cmath>

namespace pelpaint::filters {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] inline int  pixIdx(int x, int y, int w) noexcept { return y * w + x; }
[[nodiscard]] inline bool inBounds(int x, int y, int w, int h) noexcept {
    return x >= 0 && x < w && y >= 0 && y < h;
}
[[nodiscard]] inline uint8_t clamp8(int v) noexcept {
    return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Colour metric helpers
// ---------------------------------------------------------------------------

float ColorDistance(const Pixel& a, const Pixel& b) noexcept {
    const int dr = static_cast<int>(a.r) - b.r;
    const int dg = static_cast<int>(a.g) - b.g;
    const int db = static_cast<int>(a.b) - b.b;
    const int da = static_cast<int>(a.a) - b.a;
    return std::sqrt(static_cast<float>(dr*dr + dg*dg + db*db + da*da));
}

Pixel FindNearest(const Pixel& src, std::span<const Pixel> palette) noexcept {
    if (palette.empty()) return src;
    const Pixel* best     = &palette[0];
    float        bestDist = ColorDistance(src, palette[0]);
    for (const auto& p : palette) {
        const float d = ColorDistance(src, p);
        if (d < bestDist) { bestDist = d; best = &p; }
    }
    return *best;
}

// ---------------------------------------------------------------------------
// ToGrayscale
// ---------------------------------------------------------------------------

FilterResult ToGrayscale(std::span<const Pixel> src) {
    std::vector<Pixel> out;
    try { out.assign(src.begin(), src.end()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (auto& px : out) {
        const uint8_t g = clamp8(
            static_cast<int>(0.299f * px.r + 0.587f * px.g + 0.114f * px.b));
        px.r = px.g = px.b = g;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Floyd-Steinberg  (7 3 5 1  / 16 kernel)
// ---------------------------------------------------------------------------

FilterResult FloydSteinberg(
    std::span<const Pixel> src,
    int w, int h,
    std::span<const Pixel> palette,
    bool preserveAlpha)
{
    if (palette.empty())                 return std::unexpected(Error::EmptyPalette());
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());

    std::vector<Pixel> buf;
    try { buf.assign(src.begin(), src.end()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Pixel old = buf[pixIdx(x, y, w)];
            Pixel       qnt = FindNearest(old, palette);
            if (preserveAlpha) qnt.a = old.a;
            buf[pixIdx(x, y, w)] = qnt;

            const int er = old.r - qnt.r;
            const int eg = old.g - qnt.g;
            const int eb = old.b - qnt.b;

            auto spread = [&](int nx, int ny, int num, int den) noexcept {
                if (!inBounds(nx, ny, w, h)) return;
                Pixel& n = buf[pixIdx(nx, ny, w)];
                n.r = clamp8(n.r + er * num / den);
                n.g = clamp8(n.g + eg * num / den);
                n.b = clamp8(n.b + eb * num / den);
            };
            spread(x+1, y,   7, 16);
            spread(x-1, y+1, 3, 16);
            spread(x,   y+1, 5, 16);
            spread(x+1, y+1, 1, 16);
        }
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Atkinson  (1/8 kernel — distributes 6/8 of the error)
// ---------------------------------------------------------------------------

FilterResult Atkinson(
    std::span<const Pixel> src,
    int w, int h,
    std::span<const Pixel> palette,
    bool preserveAlpha)
{
    if (palette.empty())                 return std::unexpected(Error::EmptyPalette());
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());

    std::vector<Pixel> buf;
    try { buf.assign(src.begin(), src.end()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Pixel old = buf[pixIdx(x, y, w)];
            Pixel       qnt = FindNearest(old, palette);
            if (preserveAlpha) qnt.a = old.a;
            buf[pixIdx(x, y, w)] = qnt;

            const int er = old.r - qnt.r;
            const int eg = old.g - qnt.g;
            const int eb = old.b - qnt.b;

            // Six neighbours each receive 1/8 of the error.
            auto spread = [&](int nx, int ny) noexcept {
                if (!inBounds(nx, ny, w, h)) return;
                Pixel& n = buf[pixIdx(nx, ny, w)];
                n.r = clamp8(n.r + er / 8);
                n.g = clamp8(n.g + eg / 8);
                n.b = clamp8(n.b + eb / 8);
            };
            spread(x+1, y  );
            spread(x+2, y  );
            spread(x-1, y+1);
            spread(x,   y+1);
            spread(x+1, y+1);
            spread(x,   y+2);
        }
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Stucki  (wide kernel, denominator 42)
// ---------------------------------------------------------------------------
//
// Row  0:              X   8   4
// Row +1:  2   4   8   4   2
// Row +2:  1   2   4   2   1

FilterResult Stucki(
    std::span<const Pixel> src,
    int w, int h,
    std::span<const Pixel> palette,
    bool preserveAlpha)
{
    if (palette.empty())                 return std::unexpected(Error::EmptyPalette());
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());

    std::vector<Pixel> buf;
    try { buf.assign(src.begin(), src.end()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Pixel old = buf[pixIdx(x, y, w)];
            Pixel       qnt = FindNearest(old, palette);
            if (preserveAlpha) qnt.a = old.a;
            buf[pixIdx(x, y, w)] = qnt;

            const int er = old.r - qnt.r;
            const int eg = old.g - qnt.g;
            const int eb = old.b - qnt.b;

            auto spread = [&](int nx, int ny, int num) noexcept {
                if (!inBounds(nx, ny, w, h)) return;
                Pixel& n = buf[pixIdx(nx, ny, w)];
                n.r = clamp8(n.r + er * num / 42);
                n.g = clamp8(n.g + eg * num / 42);
                n.b = clamp8(n.b + eb * num / 42);
            };
            // Row 0
            spread(x+1, y,    8);
            spread(x+2, y,    4);
            // Row +1
            spread(x-2, y+1,  2);
            spread(x-1, y+1,  4);
            spread(x,   y+1,  8);
            spread(x+1, y+1,  4);
            spread(x+2, y+1,  2);
            // Row +2
            spread(x-2, y+2,  1);
            spread(x-1, y+2,  2);
            spread(x,   y+2,  4);
            spread(x+1, y+2,  2);
            spread(x+2, y+2,  1);
        }
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Ordered dithering  (Bayer 4x4 threshold matrix)
// ---------------------------------------------------------------------------

FilterResult OrderedDithering(
    std::span<const Pixel> src,
    int w, int h,
    std::span<const Pixel> palette,
    bool preserveAlpha)
{
    if (palette.empty())                 return std::unexpected(Error::EmptyPalette());
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());

    static constexpr int kBayer[4][4] = {
        { 0,  8,  2, 10},
        {12,  4, 14,  6},
        { 3, 11,  1,  9},
        {15,  7, 13,  5},
    };

    std::vector<Pixel> out;
    try { out.reserve(src.size()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Pixel p  = src[pixIdx(x, y, w)];
            const int   dv = kBayer[y % 4][x % 4];
            const Pixel dithered{
                clamp8(static_cast<int>(p.r) + dv - 8),
                clamp8(static_cast<int>(p.g) + dv - 8),
                clamp8(static_cast<int>(p.b) + dv - 8),
                p.a,
            };
            Pixel qnt = FindNearest(dithered, palette);
            if (preserveAlpha) qnt.a = p.a;
            out.push_back(qnt);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// QuantiseToPalette
// ---------------------------------------------------------------------------

FilterResult QuantiseToPalette(
    std::span<const Pixel> src,
    std::span<const Pixel> palette)
{
    if (palette.empty()) return std::unexpected(Error::EmptyPalette());

    std::vector<Pixel> out;
    try { out.reserve(src.size()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (const auto& p : src)
        out.push_back(FindNearest(p, palette));

    return out;
}

// ---------------------------------------------------------------------------
// Pixelify
// ---------------------------------------------------------------------------

FilterResult Pixelify(
    std::span<const Pixel> src,
    int w, int h,
    int blockSize,
    std::span<const Pixel> palette)
{
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());
    if (blockSize < 1)
        return std::unexpected(Error{ErrorCode::InvalidGridSize, "blockSize must be >= 1"});

    std::vector<Pixel> out;
    try { out.assign(src.begin(), src.end()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (int by = 0; by < h; by += blockSize) {
        for (int bx = 0; bx < w; bx += blockSize) {
            const int maxX = std::min(bx + blockSize, w);
            const int maxY = std::min(by + blockSize, h);

            int sumR = 0, sumG = 0, sumB = 0, sumA = 0, count = 0;
            for (int y = by; y < maxY; ++y) {
                for (int x = bx; x < maxX; ++x) {
                    const Pixel& p = src[pixIdx(x, y, w)];
                    sumR += p.r; sumG += p.g; sumB += p.b; sumA += p.a;
                    ++count;
                }
            }

            Pixel avg{
                clamp8(sumR / count),
                clamp8(sumG / count),
                clamp8(sumB / count),
                clamp8(sumA / count),
            };
            if (!palette.empty()) avg = FindNearest(avg, palette);

            for (int y = by; y < maxY; ++y)
                for (int x = bx; x < maxX; ++x)
                    out[pixIdx(x, y, w)] = avg;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Triangulate
// ---------------------------------------------------------------------------

FilterResult Triangulate(
    std::span<const Pixel> src,
    int w, int h,
    const TriangulateOptions& opts)
{
    if (src.empty() || w <= 0 || h <= 0)
        return std::unexpected(Error::InvalidDims());

    try {
        return TriangulateImage(src, w, h, opts);
    } catch (...) {
        return std::unexpected(Error::AllocFailed());
    }
}

} // namespace pelpaint::filters
