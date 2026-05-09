#pragma once

#include <expected>
#include <functional>
#include <span>
#include <vector>
#include <cstdint>

#include "../core/Error.hpp"
#include "../ColorPalettes.hpp"  // pelpaint::Pixel

namespace pelpaint::operators {

// ---------------------------------------------------------------------------
// DrawMode — governs how operators handle geometry.
// ---------------------------------------------------------------------------
enum class DrawMode : std::uint8_t {
    PixelPerfect,   ///< Integer-grid only; no interpolation; rotations at 90° increments only
    FreeDraw,       ///< Sub-pixel coordinates; bilinear interpolation; arbitrary angles
};

// ---------------------------------------------------------------------------
// OpCtx — context passed to every operator function.
// ---------------------------------------------------------------------------
struct OpCtx {
    int      width  = 0;   ///< Current pixel width of the working buffer
    int      height = 0;   ///< Current pixel height of the working buffer
    DrawMode mode   = DrawMode::PixelPerfect;
};

// ---------------------------------------------------------------------------
// OpResult — either a new pixel buffer or an Error.
// ---------------------------------------------------------------------------
using OpResult = std::expected<std::vector<Pixel>, Error>;

// ---------------------------------------------------------------------------
// OpFn — a single operator: (immutable src pixels + context) → new pixels.
// Output buffer must have ctx.width * ctx.height entries.
// ---------------------------------------------------------------------------
using OpFn = std::function<OpResult(std::span<const Pixel>, const OpCtx&)>;

// ---------------------------------------------------------------------------
// FramePipeline — an ordered, chainable sequence of OpFns.
//
//   Usage:
//     FramePipeline p;
//     p | pp_rotate(RotateAmount::R90)
//       | pp_quantise(myPalette)
//       | pp_outline({0,0,0,255});
//
//     auto result = p.Apply(srcPixels, w, h, DrawMode::PixelPerfect);
// ---------------------------------------------------------------------------
class FramePipeline {
public:
    FramePipeline() = default;

    /// Append an operator; returns *this for inline chaining.
    FramePipeline& operator|(OpFn op);

    /// Apply all operators in sequence. Short-circuits on the first error.
    [[nodiscard]] OpResult Apply(
        std::span<const Pixel> src,
        int                    w,
        int                    h,
        DrawMode               mode = DrawMode::PixelPerfect) const;

    [[nodiscard]] bool        Empty() const noexcept { return ops_.empty(); }
    [[nodiscard]] std::size_t Size()  const noexcept { return ops_.size();  }
    void                      Clear() noexcept       { ops_.clear();        }

private:
    std::vector<OpFn> ops_;
};

} // namespace pelpaint::operators
