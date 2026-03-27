#pragma once

#include <functional>
#include <cstdint>

#include "../core/Canvas.hpp"
#include "../core/Types.hpp"

namespace pelpaint::tools {

// DrawCtx: lightweight context passed to every algorithm.
//   canvas      — the Canvas being edited (active layer is the write target)
//   inSelection — optional predicate: returns false to mask out a pixel.
//                 Passing nullptr / empty function means "whole canvas".
struct DrawCtx {
    Canvas&                          canvas;
    std::function<bool(int, int)>    inSelection;   // may be empty → no mask

    // Convenience: returns true when (x,y) passes the selection mask.
    [[nodiscard]] bool allowed(int x, int y) const noexcept {
        return !inSelection || inSelection(x, y);
    }
};

// Alpha-composite src over dst in place.
// Identical semantics to Canvas::BlendPixel (kept here for algorithms that
// hold a raw Pixel& from ActiveLayerSpan without going through PutPixel).
void BlendPixel(Pixel& dst, const Pixel& src) noexcept;

// ---------------------------------------------------------------------------
// Geometric primitives
// ---------------------------------------------------------------------------

// Filled circle centred at (cx, cy) with the given radius.
// radius == 0 → single pixel.
void DrawCircleFilled(DrawCtx& ctx,
                      int cx, int cy,
                      int radius,
                      const Pixel& color);

// Outline-only circle (Bresenham midpoint algorithm).
void DrawCircleOutline(DrawCtx& ctx,
                       int cx, int cy,
                       int radius,
                       const Pixel& color);

// Bresenham line; each point on the line is stamped with a filled circle of
// diameter brushSize (rounded to nearest integer radius ≥ 0).
void DrawLineBresenham(DrawCtx& ctx,
                       int x0, int y0,
                       int x1, int y1,
                       const Pixel& color,
                       float brushSize);

// 4-connected flood fill: replaces the exact colour at (x,y) with fillColor.
void FloodFill(DrawCtx& ctx,
               int x, int y,
               const Pixel& fillColor);

// 4-connected flood fill that matches colours within a Euclidean distance
// threshold (0–441 for RGBA).
void FloodFillThreshold(DrawCtx& ctx,
                        int x, int y,
                        const Pixel& fillColor,
                        float threshold);

// Scatter numDots (derived from radius²×density) random pixels within a
// circle of the given radius.
void DrawSpray(DrawCtx& ctx,
               int x, int y,
               float radius,
               const Pixel& color,
               float density);

// ---------------------------------------------------------------------------
// BrushMode::Pen  — calligraphic pressure/tilt stroke
//
// Inspired by SDL pen/01-drawing-lines: harder pressure → wider nib.
//
//   pressure   0..1   — scales nib width  (0 = hairline, 1 = full brushSize)
//   tiltX/Y   −1..1  — pen tilt axes (SDL_PEN_AXIS_XTILT / YTILT).
//                       At (0,0) the nib is circular.  Non-zero tilt rotates
//                       and flattens the nib into a calligraphic ellipse whose
//                       major axis is perpendicular to the tilt direction.
//
// Alpha of each stamp is modulated by pressure × color.a.
// A Bresenham walk from (x0,y0) to (x1,y1) stamps a rotated ellipse at
// every step so the stroke is continuous regardless of speed.
void DrawPenStroke(DrawCtx& ctx,
                   int x0, int y0,
                   int x1, int y1,
                   const Pixel& color,
                   float brushSize,
                   float pressure,
                   float tiltX,
                   float tiltY);

// ---------------------------------------------------------------------------
// BrushMode::PixelBrush  — watercolor / wet-ink scatter
//
//   • A tight solid-core circle is always drawn at (cx, cy).
//   • N radial "rays" are march-scattered outward from the core; each ray
//     places pixels with probability that decays with distance so the result
//     looks like ink bleeding into wet paper.
//   • accumulation grows while the pen is held (call-site increments it each
//     frame) — the spread radius extends with accumulation, simulating ink
//     absorbing into the paper over time.
//   • Pixels near the outer fringe receive a slight wet-edge darkening
//     (pigment concentrates at the drying edge in real watercolour).
//
//   pressure      0..1  — core opacity and maximum scatter radius.
//   accumulation  0..N  — dwell factor; reset to 0 at stroke start.

void DrawPixelBrush(DrawCtx& ctx,
                    int cx, int cy,
                    float radius,
                    const Pixel& color,
                    float pressure,
                    float accumulation);

// Colour metric: Euclidean distance in RGBA space (0 … ~441);
[[nodiscard]] float ColorDistance(const Pixel& a, const Pixel& b) noexcept;

} // namespace pelpaint::tools
