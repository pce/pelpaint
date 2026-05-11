#pragma once

/** @file @brief Shared internal utilities for pelpaint filter translation units. */

#include <cstdint>
#include <expected>
#include <vector>

#include "../core/Types.hpp"
#include "../core/Error.hpp"

namespace pelpaint::filters {

/// Common result type: new pixel buffer on success, Error on failure.
using FilterResult = std::expected<std::vector<Pixel>, Error>;

} // namespace pelpaint::filters

namespace pelpaint::filters::detail {

/// Flat index into a width-stride pixel buffer.
[[nodiscard]] inline int pix_idx(int x, int y, int w) noexcept
    { return y * w + x; }

/// True when (x, y) is inside the [0,w) × [0,h) canvas.
[[nodiscard]] inline bool in_bounds(int x, int y, int w, int h) noexcept
    { return x >= 0 && x < w && y >= 0 && y < h; }

/// Saturate an integer to the [0, 255] uint8_t range.
[[nodiscard]] inline uint8_t clamp8(int v) noexcept
    { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

} // namespace pelpaint::filters::detail
