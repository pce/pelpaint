#pragma once

/** @file @brief Colour-space transforms and nearest-colour lookup (lowest-level filter primitives). */

#include <span>

#include "detail.hpp"   // FilterResult, Pixel, Error

namespace pelpaint::filters {

/// Euclidean RGBA distance, result in [0, ~441].
[[nodiscard]] float color_distance(const Pixel& a, const Pixel& b) noexcept;

/// Nearest-neighbour palette lookup.
/// Returns @p src unchanged when @p palette is empty.
[[nodiscard]] Pixel find_nearest(
    const Pixel&           src,
    std::span<const Pixel> palette) noexcept;

/// Convert every pixel to grayscale (BT.601 luma, alpha preserved).
[[nodiscard]] FilterResult to_grayscale(std::span<const Pixel> src);

} // namespace pelpaint::filters
