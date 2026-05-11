// filters/dithering.cpp

#include "dithering.hpp"
#include "detail.hpp"

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
