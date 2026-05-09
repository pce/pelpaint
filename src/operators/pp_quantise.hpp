#pragma once

#include "FrameOperator.hpp"
#include "../filters/Filters.hpp"

namespace pelpaint::operators {

// ---------------------------------------------------------------------------
// pp_quantise  —  snap every pixel to the nearest palette entry.
// Delegates to filters::QuantiseToPalette which uses Euclidean RGBA distance.
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn pp_quantise(std::vector<Pixel> palette) {
    return [pal = std::move(palette)](
               std::span<const Pixel> src, const OpCtx&) -> OpResult {
        return filters::QuantiseToPalette(src, std::span<const Pixel>{pal});
    };
}

// ---------------------------------------------------------------------------
// pp_grayscale  —  BT.601 luma conversion; alpha is preserved.
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn pp_grayscale() {
    return [](std::span<const Pixel> src, const OpCtx&) -> OpResult {
        return filters::ToGrayscale(src);
    };
}

// ---------------------------------------------------------------------------
// pp_pixelify  —  block-average pixelation with optional palette snap.
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn pp_pixelify(int blockSize, std::vector<Pixel> palette = {}) {
    return [blockSize, pal = std::move(palette)](
               std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        return filters::Pixelify(src, ctx.width, ctx.height, blockSize,
                                 std::span<const Pixel>{pal});
    };
}

// ---------------------------------------------------------------------------
// pp_dither_fs  —  Floyd-Steinberg dithering onto a palette.
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn pp_dither_fs(std::vector<Pixel> palette,
                                        bool               preserveAlpha = false) {
    return [pal = std::move(palette), preserveAlpha](
               std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        return filters::FloydSteinberg(src, ctx.width, ctx.height,
                                       std::span<const Pixel>{pal}, preserveAlpha);
    };
}

} // namespace pelpaint::operators
