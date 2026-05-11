// filters/colorspace.cpp

#include "colorspace.hpp"

#include <cmath>

namespace pelpaint::filters {

// ---------------------------------------------------------------------------
// color_distance
// ---------------------------------------------------------------------------

float color_distance(const Pixel& a, const Pixel& b) noexcept
{
    const int dr = static_cast<int>(a.r) - b.r;
    const int dg = static_cast<int>(a.g) - b.g;
    const int db = static_cast<int>(a.b) - b.b;
    const int da = static_cast<int>(a.a) - b.a;
    return std::sqrt(static_cast<float>(dr*dr + dg*dg + db*db + da*da));
}

// ---------------------------------------------------------------------------
// find_nearest
// ---------------------------------------------------------------------------

Pixel find_nearest(const Pixel& src, std::span<const Pixel> palette) noexcept
{
    if (palette.empty()) return src;
    const Pixel* best     = &palette[0];
    float        bestDist = color_distance(src, palette[0]);
    for (const auto& p : palette) {
        const float d = color_distance(src, p);
        if (d < bestDist) { bestDist = d; best = &p; }
    }
    return *best;
}

// ---------------------------------------------------------------------------
// to_grayscale
// ---------------------------------------------------------------------------

FilterResult to_grayscale(std::span<const Pixel> src)
{
    std::vector<Pixel> out;
    try { out.assign(src.begin(), src.end()); }
    catch (...) { return std::unexpected(Error::AllocFailed()); }

    for (auto& px : out) {
        const uint8_t g = detail::clamp8(
            static_cast<int>(0.299f * px.r + 0.587f * px.g + 0.114f * px.b));
        px.r = px.g = px.b = g;
    }
    return out;
}

} // namespace pelpaint::filters
