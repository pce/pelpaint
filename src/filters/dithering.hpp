#pragma once

/** @file @brief Error-diffusion and ordered dithering algorithms. */

#include <span>

#include "colorspace.hpp"   // FilterResult + find_nearest (used in impls)

namespace pelpaint::filters {

/// Floyd-Steinberg error-diffusion (7 3 5 1 / 16 kernel).
/// @param palette       Target palette — must not be empty.
/// @param preserve_alpha Copy source alpha verbatim into the output.
[[nodiscard]] FilterResult floyd_steinberg(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    bool                   preserve_alpha = false);

/// Atkinson error-diffusion (1/8-weight kernel, distributes 6/8 of the error).
[[nodiscard]] FilterResult atkinson(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    bool                   preserve_alpha = false);

/// Stucki error-diffusion (wide-kernel, denominator 42).
[[nodiscard]] FilterResult stucki(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    bool                   preserve_alpha = false);

/// Bayer 4×4 ordered (threshold-matrix) dithering.
[[nodiscard]] FilterResult ordered_dithering(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    bool                   preserve_alpha = false);

} // namespace pelpaint::filters
