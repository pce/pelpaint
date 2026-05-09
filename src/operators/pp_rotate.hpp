#pragma once

#include "FrameOperator.hpp"
#include <algorithm>

namespace pelpaint::operators {

enum class RotateAmount : std::uint8_t { R90, R180, R270 };

// ---------------------------------------------------------------------------
// pp_rotate  —  PixelPerfect 90 / 180 / 270° clockwise rotation.
//
// Non-square images: the output is fitted back to the original w×h canvas by
// cropping any pixels that fall outside and leaving the remainder transparent.
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn pp_rotate(RotateAmount rot) {
    return [rot](std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        if (src.empty() || ctx.width <= 0 || ctx.height <= 0)
            return std::unexpected(Error::InvalidDims());

        const int w = ctx.width, h = ctx.height;
        std::vector<Pixel> out(static_cast<std::size_t>(w * h), Pixel{0, 0, 0, 0});

        switch (rot) {
        case RotateAmount::R90:
            // 90° CW: dst(h-1-y, x)  (in a h×w buffer); crop/pad back to w×h
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const int nx = h - 1 - y;   // column in h×w space
                    const int ny = x;            // row    in h×w space
                    if (nx < w && ny < h)        // fit back into original w×h
                        out[ny * w + nx] = src[y * w + x];
                }
            break;

        case RotateAmount::R180:
            for (int i = 0, n = w * h; i < n; ++i)
                out[n - 1 - i] = src[i];
            break;

        case RotateAmount::R270:
            // 270° CW (= 90° CCW): dst(y, w-1-x)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const int nx = y;
                    const int ny = w - 1 - x;
                    if (nx < w && ny < h)
                        out[ny * w + nx] = src[y * w + x];
                }
            break;
        }
        return out;
    };
}

// ---------------------------------------------------------------------------
// pp_flip_h  —  horizontal (left-right) mirror.
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn pp_flip_h() {
    return [](std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        if (src.empty() || ctx.width <= 0 || ctx.height <= 0)
            return std::unexpected(Error::InvalidDims());
        const int w = ctx.width, h = ctx.height;
        std::vector<Pixel> out(src.begin(), src.end());
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w / 2; ++x)
                std::swap(out[y * w + x], out[y * w + (w - 1 - x)]);
        return out;
    };
}

// ---------------------------------------------------------------------------
// pp_flip_v  —  vertical (top-bottom) mirror.
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn pp_flip_v() {
    return [](std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        if (src.empty() || ctx.width <= 0 || ctx.height <= 0)
            return std::unexpected(Error::InvalidDims());
        const int w = ctx.width, h = ctx.height;
        std::vector<Pixel> out(src.begin(), src.end());
        for (int y = 0; y < h / 2; ++y)
            for (int x = 0; x < w; ++x)
                std::swap(out[y * w + x], out[(h - 1 - y) * w + x]);
        return out;
    };
}

} // namespace pelpaint::operators
