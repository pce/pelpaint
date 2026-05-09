#pragma once

#include "FrameOperator.hpp"

namespace pelpaint::operators {

// ---------------------------------------------------------------------------
// pp_offset  —  integer-pixel translation.
//
// Pixels scrolled out of bounds are lost; new areas are transparent.
// In both PixelPerfect and FreeDraw mode the offset is applied as whole pixels
// (call affine_translate for sub-pixel FreeDraw offsets).
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn pp_offset(int dx, int dy) {
    return [dx, dy](std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        if (src.empty()) return std::unexpected(Error::InvalidDims());
        const int w = ctx.width, h = ctx.height;
        std::vector<Pixel> out(src.size(), Pixel{0, 0, 0, 0});
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < w && ny >= 0 && ny < h)
                    out[ny * w + nx] = src[y * w + x];
            }
        }
        return out;
    };
}

} // namespace pelpaint::operators
