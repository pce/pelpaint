#pragma once

#include "FrameOperator.hpp"

namespace pelpaint::operators {

// ---------------------------------------------------------------------------
// pp_outline  —  8-connected outline drawn in outlineColor around
//                every opaque pixel (alpha >= alphaThreshold).
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn pp_outline(Pixel outlineColor,
                                      std::uint8_t alphaThreshold = 1) {
    return [outlineColor, alphaThreshold](
               std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        if (src.empty()) return std::unexpected(Error::InvalidDims());
        const int w = ctx.width, h = ctx.height;
        std::vector<Pixel> out(src.begin(), src.end());

        constexpr int ndx[] = {-1,  0,  1, -1, 1, -1,  0,  1};
        constexpr int ndy[] = {-1, -1, -1,  0, 0,  1,  1,  1};

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (src[y * w + x].a >= alphaThreshold) continue;
                for (int d = 0; d < 8; ++d) {
                    const int nx = x + ndx[d], ny = y + ndy[d];
                    if (nx >= 0 && nx < w && ny >= 0 && ny < h &&
                        src[ny * w + nx].a >= alphaThreshold) {
                        out[y * w + x] = outlineColor;
                        break;
                    }
                }
            }
        }
        return out;
    };
}

// ---------------------------------------------------------------------------
// pp_shadow  —  drop-shadow: offset copy of opaque pixels drawn below original.
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn pp_shadow(Pixel        shadowColor,
                                     int          offsetX         = 2,
                                     int          offsetY         = 2,
                                     std::uint8_t alphaThreshold  = 1) {
    return [shadowColor, offsetX, offsetY, alphaThreshold](
               std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        if (src.empty()) return std::unexpected(Error::InvalidDims());
        const int w = ctx.width, h = ctx.height;
        std::vector<Pixel> out(src.size(), Pixel{0, 0, 0, 0});

        // Draw shadow first
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (src[y * w + x].a < alphaThreshold) continue;
                const int sx = x + offsetX, sy = y + offsetY;
                if (sx >= 0 && sx < w && sy >= 0 && sy < h)
                    out[sy * w + sx] = shadowColor;
            }
        // Original on top
        for (int i = 0, n = w * h; i < n; ++i)
            if (src[i].a >= alphaThreshold) out[i] = src[i];

        return out;
    };
}

} // namespace pelpaint::operators
