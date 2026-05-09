#pragma once

// ---------------------------------------------------------------------------
// pp_morph — pixel-wise linear blend between the pipeline src and a fixed
// target frame.
//
//   t = 0.0  →  pure src (identity)
//   t = 1.0  →  pure target
//
// Primary use-case: smoothing pass between two consecutively baked frames
// without the expense of a full re-bake.  Feed pp_morph into a pipeline
// alongside any other operator to crossfade the output.
//
// See also: effects::InsertSmoothFrames() which builds the interpolated
// frames without a pipeline by directly writing lerped surfaces.
// ---------------------------------------------------------------------------

#include "FrameOperator.hpp"
#include "../core/Error.hpp"

namespace pelpaint::operators {

/// Pixel-wise linear interpolation toward `target`.
/// `target` must have the same pixel count as the source span; the operator
/// returns an error (InvalidDimensions) if the sizes differ.
[[nodiscard]] inline OpFn pp_morph(std::vector<Pixel> target, float t) noexcept
{
    return [tgt = std::move(target), t]
           (std::span<const Pixel> src, const OpCtx&) -> OpResult
    {
        if (src.size() != tgt.size())
            return std::unexpected(pelpaint::Error{
                pelpaint::ErrorCode::InvalidDimensions,
                "pp_morph: src and target pixel counts differ"});

        std::vector<Pixel> out(src.size());

        const auto lc = [](uint8_t a, uint8_t b, float f) -> uint8_t {
            return static_cast<uint8_t>(
                static_cast<float>(a)
                + f * (static_cast<float>(b) - static_cast<float>(a)));
        };

        for (std::size_t i = 0; i < src.size(); ++i) {
            out[i] = {
                lc(src[i].r, tgt[i].r, t),
                lc(src[i].g, tgt[i].g, t),
                lc(src[i].b, tgt[i].b, t),
                lc(src[i].a, tgt[i].a, t),
            };
        }
        return out;
    };
}

} // namespace pelpaint::operators
