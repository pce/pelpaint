#pragma once

#include "FrameOperator.hpp"
#include "../filters/Filters.hpp"

namespace pelpaint::operators {

[[nodiscard]] inline OpFn pp_quantise(std::vector<Pixel> palette) {
    return [pal = std::move(palette)](
               std::span<const Pixel> src, const OpCtx&) -> OpResult {
        return filters::quantise_to_palette(src, std::span<const Pixel>{pal});
    };
}

[[nodiscard]] inline OpFn pp_grayscale() {
    return [](std::span<const Pixel> src, const OpCtx&) -> OpResult {
        return filters::to_grayscale(src);
    };
}

[[nodiscard]] inline OpFn pp_pixelify(int blockSize, std::vector<Pixel> palette = {}) {
    return [blockSize, pal = std::move(palette)](
               std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        return filters::pixelify(src, ctx.width, ctx.height, blockSize,
                                 std::span<const Pixel>{pal});
    };
}

[[nodiscard]] inline OpFn pp_dither_fs(std::vector<Pixel> palette,
                                        bool               preserveAlpha = false) {
    return [pal = std::move(palette), preserveAlpha](
               std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        return filters::floyd_steinberg(src, ctx.width, ctx.height,
                                        std::span<const Pixel>{pal}, preserveAlpha);
    };
}

} // namespace pelpaint::operators
