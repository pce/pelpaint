#pragma once

/** @file @brief Umbrella filter header — includes colorspace and dithering, declares pixel-art and convolution filters. */

#include <span>
#include <vector>
#include <cstdint>

#include "colorspace.hpp"  // FilterResult, color_distance, find_nearest, to_grayscale
#include "dithering.hpp"   // floyd_steinberg, atkinson, stucki, ordered_dithering

namespace pelpaint::filters {

/// Snap every pixel to the nearest colour in the palette.
[[nodiscard]] FilterResult quantise_to_palette(
    std::span<const Pixel> src,
    std::span<const Pixel> palette);

/// Pixelate: average blockSize×blockSize blocks, optionally snap to palette.
/// Pass an empty @p palette span to skip palette quantisation.
[[nodiscard]] FilterResult pixelify(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    int                    blockSize,
    std::span<const Pixel> palette = {});

/// Box blur (radius=1 → 3×3). gaussian=true applies three box-blur passes
/// (Langer's approximation of a Gaussian at the same radius).
[[nodiscard]] FilterResult blur(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    int                    radius,
    bool                   gaussian = false);

/// 3×3 unsharp-mask sharpen.
/// strength ∈ [0, 2]: 0 = identity, 1 = standard sharpen, 2 = over-sharpen.
[[nodiscard]] FilterResult sharpen(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    float                  strength = 1.0f);

enum class EdgeDetectMode { Sobel, Laplacian };

/// Luminance-based Sobel or Laplacian edge detection.
/// Output: bright edges on black (or inverted). Alpha is preserved.
/// Pixels whose edge magnitude is below `threshold` are set to black.
[[nodiscard]] FilterResult edge_detect(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    EdgeDetectMode         mode         = EdgeDetectMode::Sobel,
    float                  threshold    = 30.f,
    bool                   invert_output = false);

enum class OutlineMode { Outline, Rim };
enum class OutlineEdge { PixelPerfect, Opacity };

struct OutlineConfig {
    OutlineMode  mode           = OutlineMode::Outline;
    OutlineEdge  edge_mode      = OutlineEdge::PixelPerfect;
    int          pen_size       = 2;           ///< ring thickness in pixels [1, 32]
    Pixel        color          = {0,0,0,255};
    bool         auto_lighten   = false;       ///< override color with lightened version
    float        lighten_factor = 0.4f;        ///< [0,1] — 0=identity, 1=white
    uint8_t      alpha_threshold = 10;         ///< min alpha to treat pixel as opaque
};

/// Compute a ring outline or rim around opaque pixels.
/// Returns a pixel buffer (same W×H as src), fully transparent except the ring.
/// Caller adds it as a new canvas layer and calls WriteFlat.
[[nodiscard]] FilterResult outline_layer(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    const OutlineConfig&   cfg);

struct TriangulateOptions {
    int     numPoints      = 300;   ///< Number of Delaunay control points
    int     mutations      = 2;     ///< Point mutations per member per generation
    float   variation      = 0.30f; ///< Mutation magnitude as fraction of max(W,H)
    int     population     = 100;   ///< GA population size
    int     cutoff         = 5;     ///< Top members kept as elites each generation
    int     generations    = 30;    ///< Number of GA generations to run
    bool    edgeBias       = true;  ///< Bias initial point placement toward edges
    uint8_t alphaThreshold = 10;    ///< Min alpha to treat a pixel as non-transparent
};

/// Triangulate the image using a Triangula-style genetic algorithm.
/// Returns a new pixel buffer (same dimensions as input) rendered as
/// non-overlapping Delaunay triangles flat-shaded with the average colour
/// of all source pixels inside each triangle.
/// Transparent pixels (alpha < alphaThreshold) are preserved unchanged.
[[nodiscard]] FilterResult triangulate(
    std::span<const Pixel>    src,
    int                       w,
    int                       h,
    const TriangulateOptions& opts = {});

} // namespace pelpaint::filters
