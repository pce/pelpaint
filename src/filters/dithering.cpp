// filters/dithering.cpp

#include "dithering.hpp"
#include "detail.hpp"
#include <algorithm>

namespace pelpaint::filters {

using namespace detail;

// ---------------------------------------------------------------------------
// floyd_steinberg   (7 3 5 1 / 16 kernel)
// ---------------------------------------------------------------------------

FilterResult floyd_steinberg(
    std::span<const Pixel> src,
    int w, int h,
    std::span<const Pixel> palette,
    bool preserve_alpha)
{
    if (palette.empty())                 return std::unexpected(Error::EmptyPalette());
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());

    std::vector<Pixel> buf;
    try { buf.assign(src.begin(), src.end()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Pixel old = buf[pix_idx(x, y, w)];
            Pixel       qnt = find_nearest(old, palette);
            if (preserve_alpha) qnt.a = old.a;
            buf[pix_idx(x, y, w)] = qnt;

            const int er = old.r - qnt.r;
            const int eg = old.g - qnt.g;
            const int eb = old.b - qnt.b;

            auto spread = [&](int nx, int ny, int num, int den) noexcept {
                if (!in_bounds(nx, ny, w, h)) return;
                Pixel& n = buf[pix_idx(nx, ny, w)];
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
// atkinson   (1/8 kernel — distributes 6/8 of the error)
// ---------------------------------------------------------------------------

FilterResult atkinson(
    std::span<const Pixel> src,
    int w, int h,
    std::span<const Pixel> palette,
    bool preserve_alpha)
{
    if (palette.empty())                 return std::unexpected(Error::EmptyPalette());
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());

    std::vector<Pixel> buf;
    try { buf.assign(src.begin(), src.end()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Pixel old = buf[pix_idx(x, y, w)];
            Pixel       qnt = find_nearest(old, palette);
            if (preserve_alpha) qnt.a = old.a;
            buf[pix_idx(x, y, w)] = qnt;

            const int er = old.r - qnt.r;
            const int eg = old.g - qnt.g;
            const int eb = old.b - qnt.b;

            // Six neighbours each receive 1/8 of the error.
            auto spread = [&](int nx, int ny) noexcept {
                if (!in_bounds(nx, ny, w, h)) return;
                Pixel& n = buf[pix_idx(nx, ny, w)];
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
// stucki   (wide kernel, denominator 42)
//
// Row  0:              X   8   4
// Row +1:  2   4   8   4   2
// Row +2:  1   2   4   2   1
// ---------------------------------------------------------------------------

FilterResult stucki(
    std::span<const Pixel> src,
    int w, int h,
    std::span<const Pixel> palette,
    bool preserve_alpha)
{
    if (palette.empty())                 return std::unexpected(Error::EmptyPalette());
    if (src.empty() || w <= 0 || h <= 0) return std::unexpected(Error::InvalidDims());

    std::vector<Pixel> buf;
    try { buf.assign(src.begin(), src.end()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Pixel old = buf[pix_idx(x, y, w)];
            Pixel       qnt = find_nearest(old, palette);
            if (preserve_alpha) qnt.a = old.a;
            buf[pix_idx(x, y, w)] = qnt;

            const int er = old.r - qnt.r;
            const int eg = old.g - qnt.g;
            const int eb = old.b - qnt.b;

            auto spread = [&](int nx, int ny, int num) noexcept {
                if (!in_bounds(nx, ny, w, h)) return;
                Pixel& n = buf[pix_idx(nx, ny, w)];
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
// ordered_dithering   (Bayer 4x4 threshold matrix)
// ---------------------------------------------------------------------------

FilterResult ordered_dithering(
    std::span<const Pixel> src,
    int w, int h,
    std::span<const Pixel> palette,
    bool preserve_alpha)
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
            const Pixel p  = src[pix_idx(x, y, w)];
            const int   dv = kBayer[y % 4][x % 4];
            const Pixel dithered{
                clamp8(static_cast<int>(p.r) + dv - 8),
                clamp8(static_cast<int>(p.g) + dv - 8),
                clamp8(static_cast<int>(p.b) + dv - 8),
                p.a,
            };
            Pixel qnt = find_nearest(dithered, palette);
            if (preserve_alpha) qnt.a = p.a;
            out.push_back(qnt);
        }
    }
    return out;
}

} // namespace pelpaint::filters

// ============================================================================
// Dithering 2.0 helpers
// ============================================================================

namespace pelpaint::filters {

std::vector<Pixel> normalize_contrast(std::span<const Pixel> src)
{
    if (src.empty()) return {};

    uint8_t rMin = 255, rMax = 0;
    uint8_t gMin = 255, gMax = 0;
    uint8_t bMin = 255, bMax = 0;

    for (const auto& p : src) {
        rMin = std::min(rMin, p.r); rMax = std::max(rMax, p.r);
        gMin = std::min(gMin, p.g); gMax = std::max(gMax, p.g);
        bMin = std::min(bMin, p.b); bMax = std::max(bMax, p.b);
    }

    auto stretch = [](uint8_t v, uint8_t lo, uint8_t hi) -> uint8_t {
        if (hi == lo) return v;
        return static_cast<uint8_t>(((static_cast<int>(v) - lo) * 255) / (hi - lo));
    };

    std::vector<Pixel> out;
    out.reserve(src.size());
    for (const auto& p : src)
        out.push_back({
            stretch(p.r, rMin, rMax),
            stretch(p.g, gMin, gMax),
            stretch(p.b, bMin, bMax),
            p.a
        });
    return out;
}

std::vector<Pixel> scale_nearest(
    std::span<const Pixel> src,
    int srcW, int srcH,
    int dstW, int dstH)
{
    std::vector<Pixel> out;
    out.reserve(static_cast<std::size_t>(dstW) * static_cast<std::size_t>(dstH));

    for (int y = 0; y < dstH; ++y) {
        const int sy = (y * srcH) / dstH;
        for (int x = 0; x < dstW; ++x) {
            const int sx = (x * srcW) / dstW;
            out.push_back(src[static_cast<std::size_t>(sy * srcW + sx)]);
        }
    }
    return out;
}

FilterResult apply_dither_pipeline(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    DitheringFn            fn,
    const DitherOptions&   opts,
    bool                   preserve_alpha)
{
    if (src.empty() || w <= 0 || h <= 0)
        return std::unexpected(Error::InvalidDims());

    // Clamp pixelDensity to (0, 1]
    const float density = (opts.pixelDensity > 0.f)
                        ? std::min(opts.pixelDensity, 1.0f)
                        : 1.0f;

    // Compute working dimensions based on frameResolution + pixelDensity
    int workW = w;
    int workH = h;

    const int frame = static_cast<int>(opts.frameResolution);
    if (frame > 0 && (w > frame || h > frame)) {
        // Scale the longest edge to `frame`, preserve aspect ratio
        if (w >= h) {
            workW = frame;
            workH = std::max(1, (h * frame) / w);
        } else {
            workH = frame;
            workW = std::max(1, (w * frame) / h);
        }
    }

    // Apply pixelDensity on top of the frame resolution
    workW = std::max(1, static_cast<int>(static_cast<float>(workW) * density));
    workH = std::max(1, static_cast<int>(static_cast<float>(workH) * density));

    const bool needsScale = (workW != w || workH != h);

    // 1. Pre-pass: histogram normalisation
    std::vector<Pixel> preBuffer;
    std::span<const Pixel> working = src;

    if (opts.preNormalize) {
        preBuffer = normalize_contrast(src);
        working   = preBuffer;
    }

    // 2. Pre-pass: downscale to working resolution
    std::vector<Pixel> downBuffer;
    if (needsScale) {
        downBuffer = scale_nearest(working, w, h, workW, workH);
        working    = downBuffer;
    }

    // 3. Core dithering at working resolution
    auto result = fn(working, workW, workH, palette, preserve_alpha);
    if (!result) return result;

    // 4. Post-pass: nearest-neighbour upscale back to original dimensions
    if (needsScale && opts.postNearestScale) {
        auto upscaled = scale_nearest(*result, workW, workH, w, h);
        return upscaled;
    }

    return result;
}

} // namespace pelpaint::filters
