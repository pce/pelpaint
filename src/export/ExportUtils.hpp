#pragma once

#include <cmath>
#include <algorithm>
#include <fstream>
#include <limits>
#include "../PixelPaintView.hpp"

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

static inline bool ReadPixelRGBA8(const ImageView& view,
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


} // namespace pelpaint::exporter
