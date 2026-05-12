/// @file PaletteRef.cpp
/// @brief PaletteRef resolve() and factory-helper implementations.
#include "PaletteRef.hpp"
#include "PaletteLibrary.hpp"
#include <algorithm>
#include <unordered_map>
#include <ranges>

namespace pelpaint {

namespace {

/// Extract up to @p maxColors unique RGBA colours from @p pixels, ordered by
/// frequency descending (most common first).
std::vector<Pixel> extract_unique_colors(std::span<const Pixel> pixels, int maxColors)
{
    std::unordered_map<uint32_t, int> freq;
    freq.reserve(std::min(pixels.size(), std::size_t(maxColors) * 4));

    for (const auto& p : pixels) {
        const uint32_t key = (uint32_t(p.r) << 24) | (uint32_t(p.g) << 16)
                           | (uint32_t(p.b) <<  8) |  uint32_t(p.a);
        ++freq[key];
    }

    std::vector<std::pair<int, uint32_t>> sorted;
    sorted.reserve(freq.size());
    for (const auto& [k, v] : freq)
        sorted.emplace_back(v, k);

    std::ranges::sort(sorted, std::greater<int>{}, &std::pair<int, uint32_t>::first);

    const int n = std::min(maxColors, static_cast<int>(sorted.size()));
    std::vector<Pixel> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const uint32_t k = sorted[i].second;
        out.push_back({ uint8_t(k >> 24), uint8_t(k >> 16), uint8_t(k >> 8), uint8_t(k) });
    }
    return out;
}

} // namespace

std::span<const Pixel> PaletteRef::resolve(
    const PaletteLibrary&  library,
    std::span<const Pixel> imagePixels,
    std::vector<Pixel>&    out_auto) const
{
    switch (source) {
    case PaletteSource::Named:
        if (namedIndex >= 0 && namedIndex < static_cast<int>(library.All().size()))
            return library.All()[namedIndex].colors;
        return {};
    case PaletteSource::Custom:
        return colors;
    case PaletteSource::Auto:
        if (imagePixels.empty()) return {};
        out_auto = extract_unique_colors(imagePixels, maxAutoColors);
        return out_auto;
    case PaletteSource::None:
        return {};
    }
    return {};
}

PaletteRef PaletteRef::fromNamed(int index) noexcept {
    PaletteRef r;
    r.source      = PaletteSource::Named;
    r.namedIndex  = index;
    return r;
}

PaletteRef PaletteRef::fromCustom(std::vector<Pixel> cols) {
    PaletteRef r;
    r.source = PaletteSource::Custom;
    r.colors = std::move(cols);
    return r;
}

PaletteRef PaletteRef::fromAuto(int maxColors) noexcept {
    PaletteRef r;
    r.source        = PaletteSource::Auto;
    r.maxAutoColors = maxColors;
    return r;
}

} // namespace pelpaint
