#pragma once

#include "FrameOperator.hpp"
#include "../filters/Filters.hpp"

namespace pelpaint::operators {

// ---------------------------------------------------------------------------
// pp_cycle_palette  —  rotate a named set of colors by `offset` steps.
//
// Classic pixel-art palette-cycling trick:
//   Each pixel whose color is within `matchThreshold` RGBA distance of any
//   entry in cycleColors is remapped to the color `offset` positions ahead
//   (wrapping).  Transparent pixels (a == 0) are always skipped.
//
//   cycleColors    — ordered list of participating colors. N = length = period.
//   offset         — steps to rotate forward (negative = backward).
//   matchThreshold — max Euclidean RGBA distance to classify a pixel as a
//                    cycle-color member.  0.5 = near-exact match only (use
//                    when the layer was pre-quantized).  Higher values give
//                    tolerance for hand-painted sources.
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn pp_cycle_palette(
    std::vector<Pixel> cycleColors,
    int                offset,
    float              matchThreshold = 0.5f)
{
    return [cyc = std::move(cycleColors), offset, matchThreshold](
               std::span<const Pixel> src, const OpCtx&) -> OpResult
    {
        const int n = static_cast<int>(cyc.size());
        if (n == 0) return std::vector<Pixel>(src.begin(), src.end());

        const int off = ((offset % n) + n) % n;
        if (off == 0) return std::vector<Pixel>(src.begin(), src.end());

        std::vector<Pixel> out(src.begin(), src.end());
        for (auto& px : out) {
            if (px.a == 0) continue;   // preserve fully transparent pixels

            // Find closest cycle-color entry within threshold.
            int   best     = -1;
            float bestDist = matchThreshold + 0.001f;
            for (int i = 0; i < n; ++i) {
                const float d = filters::ColorDistance(px, cyc[i]);
                if (d < bestDist) { bestDist = d; best = i; }
            }
            if (best >= 0)
                px = cyc[(best + off) % n];
        }
        return out;
    };
}

// ---------------------------------------------------------------------------
// SliceCycleColors  —  extract a contiguous range from a larger palette.
//
//   SliceCycleColors(pico8Palette, 6, 9)  → {pal[6], pal[7], pal[8], pal[9]}
//
// startIdx and endIdx are both inclusive.
// ---------------------------------------------------------------------------
[[nodiscard]] inline std::vector<Pixel>
SliceCycleColors(std::span<const Pixel> palette, int startIdx, int endIdx)
{
    std::vector<Pixel> result;
    const int n = static_cast<int>(palette.size());
    for (int i = startIdx; i <= endIdx && i < n; ++i)
        result.push_back(palette[i]);
    return result;
}

} // namespace pelpaint::operators
