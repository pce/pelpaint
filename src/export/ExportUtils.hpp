#pragma once

#include <array>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <limits>
#include <optional>
#include "../core/Types.hpp"
#include "../core/Error.hpp"

namespace pelpaint::exporter {

static inline std::size_t SampleWidth(std::uint32_t width, std::uint32_t gridSize) noexcept {
    return (width + gridSize - 1u) / gridSize;
}

static inline std::size_t SampleHeight(std::uint32_t height, std::uint32_t gridSize) noexcept {
    return (height + gridSize - 1u) / gridSize;
}

static inline std::uint32_t ClampU32(std::uint32_t v, std::uint32_t lo, std::uint32_t hi) noexcept {
    return std::min(std::max(v, lo), hi);
}

static inline float Clamp01(float v) noexcept {
    return std::min(std::max(v, 0.0f), 1.0f);
}

static inline float LumaFromRGBA(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    // Fast perceptual luma approximation
    return (0.2126f * static_cast<float>(r)) +
           (0.7152f * static_cast<float>(g)) +
           (0.0722f * static_cast<float>(b));
}

static inline bool ReadPixelRGBA8(const pelpaint::ImageView& view,
                                 std::uint32_t x,
                                 std::uint32_t y,
                                 std::uint8_t& r,
                                 std::uint8_t& g,
                                 std::uint8_t& b,
                                 std::uint8_t& a) noexcept
{
    if (x >= view.width || y >= view.height || view.data == nullptr || view.channels < 4) {
        return false;
    }

    const std::uint8_t* p = view.data + (static_cast<std::size_t>(y) * view.stride) +
                            (static_cast<std::size_t>(x) * view.channels);

    r = p[0];
    g = p[1];
    b = p[2];
    a = p[3];
    return true;
}

/// Type-safe pixel read returning std::expected.
/// Replaces out-params with a value; use .and_then() to chain operations.
/// @note Prefer this over ReadPixelRGBA8 in new code; enables monadic error chaining.
[[nodiscard]] static inline std::expected<pelpaint::Pixel, pelpaint::Error>
ReadPixelSafe(const pelpaint::ImageView& view,
              std::uint32_t              x,
              std::uint32_t              y) noexcept
{
    if (x >= view.width || y >= view.height ||
        view.data == nullptr || view.channels < 4)
    {
        return std::unexpected(pelpaint::Error{
            pelpaint::ErrorCode::OutOfBounds, "Pixel coord out of bounds"});
    }
    const std::uint8_t* p =
        view.data
        + (static_cast<std::size_t>(y) * view.stride)
        + (static_cast<std::size_t>(x) * view.channels);
    return pelpaint::Pixel{p[0], p[1], p[2], p[3]};
}


/// Detect the likely background colour by majority vote among the
/// four corner pixels.  Returns the corner colour that appears ≥ 2 times;
/// falls back to the top-left corner if all four differ.
///
/// Use this before PixelMesh/SvgExport to automatically identify and
/// skip the canvas background — especially for "black box on white BG"
/// or "white box on black BG" pixel art.
[[nodiscard]] static inline std::optional<pelpaint::Pixel>
DetectBackground(const pelpaint::ImageView& view) noexcept
{
    if (!view.valid() || !view.data) return std::nullopt;

    const std::uint32_t W = view.width;
    const std::uint32_t H = view.height;

    // Sample the four corners
    std::array<pelpaint::Pixel, 4> corners{};
    {
        std::uint8_t r, g, b, a;
        ReadPixelRGBA8(view, 0,   0,   r, g, b, a); corners[0] = {r,g,b,a};
        ReadPixelRGBA8(view, W-1, 0,   r, g, b, a); corners[1] = {r,g,b,a};
        ReadPixelRGBA8(view, 0,   H-1, r, g, b, a); corners[2] = {r,g,b,a};
        ReadPixelRGBA8(view, W-1, H-1, r, g, b, a); corners[3] = {r,g,b,a};
    }

    // Return the first colour that appears at least twice
    for (int i = 0; i < 4; ++i) {
        int count = 0;
        for (int j = 0; j < 4; ++j) {
            if (corners[i].r == corners[j].r &&
                corners[i].g == corners[j].g &&
                corners[i].b == corners[j].b)
                ++count;
        }
        if (count >= 2) return corners[i];
    }

    return corners[0]; // all four differ — fall back to top-left
}

/// Returns true if `pixel` matches `bg` within the given per-channel tolerance.
[[nodiscard]] static inline bool IsBackground(
    const pelpaint::Pixel& pixel,
    const pelpaint::Pixel& bg,
    std::uint8_t           tolerance = 15) noexcept
{
    auto diff = [](std::uint8_t a, std::uint8_t b) -> std::uint8_t {
        return static_cast<std::uint8_t>(a > b ? a - b : b - a);
    };
    return diff(pixel.r, bg.r) <= tolerance &&
           diff(pixel.g, bg.g) <= tolerance &&
           diff(pixel.b, bg.b) <= tolerance;
}

} // namespace pelpaint::exporter
