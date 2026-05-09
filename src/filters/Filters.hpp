#pragma once

#include <expected>
#include <span>
#include <vector>
#include <cstdint>

#include "../core/Types.hpp"
#include "../core/Error.hpp"

namespace pelpaint::filters {

// ---------------------------------------------------------------------------
// Result type
// ---------------------------------------------------------------------------

/// Success = new pixel buffer (std::vector<Pixel>).
/// Failure = pelpaint::Error (code + message, no heap allocation).
using FilterResult = std::expected<std::vector<Pixel>, Error>;

// ---------------------------------------------------------------------------
// Colour-space transforms
// ---------------------------------------------------------------------------

/// Convert every pixel to grayscale (BT.601 luma, alpha preserved).
[[nodiscard]] FilterResult ToGrayscale(std::span<const Pixel> src);

// ---------------------------------------------------------------------------
// Error-diffusion dithering
// ---------------------------------------------------------------------------

/// Floyd-Steinberg dithering (classic 7/3/5/1 kernel).
/// @param palette       Target palette — must not be empty.
/// @param preserveAlpha Copy source alpha verbatim into the output.
[[nodiscard]] FilterResult FloydSteinberg(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    bool                   preserveAlpha = false);

/// Atkinson dithering (1/8-weight kernel, distributes 6/8 of the error).
[[nodiscard]] FilterResult Atkinson(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    bool                   preserveAlpha = false);

/// Stucki dithering (wide-kernel, denominator 42).
[[nodiscard]] FilterResult Stucki(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    bool                   preserveAlpha = false);

/// Bayer 4x4 ordered (threshold-matrix) dithering.
[[nodiscard]] FilterResult OrderedDithering(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    bool                   preserveAlpha = false);

// ---------------------------------------------------------------------------
// Palette quantisation
// ---------------------------------------------------------------------------

/// Snap every pixel to the nearest colour in the palette.
[[nodiscard]] FilterResult QuantiseToPalette(
    std::span<const Pixel> src,
    std::span<const Pixel> palette);

// ---------------------------------------------------------------------------
// Pixel-art effects
// ---------------------------------------------------------------------------

/// Pixelate: average blockSize x blockSize blocks, optionally snap to palette.
/// Pass an empty @p palette span to skip palette quantisation.
[[nodiscard]] FilterResult Pixelify(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    int                    blockSize,
    std::span<const Pixel> palette = {});

// ---------------------------------------------------------------------------
// Colour metric helpers  (pure, exposed for callers that need them)
// ---------------------------------------------------------------------------

/// Euclidean RGBA distance in [0, ~441].
[[nodiscard]] float ColorDistance(const Pixel& a, const Pixel& b) noexcept;

/// Nearest-neighbour palette lookup.
/// Returns @p src unchanged when @p palette is empty.
[[nodiscard]] Pixel FindNearest(
    const Pixel&           src,
    std::span<const Pixel> palette) noexcept;

// ---------------------------------------------------------------------------
// Triangulate (Triangula-style genetic algorithm triangulation)
// ---------------------------------------------------------------------------

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
[[nodiscard]] FilterResult Triangulate(
    std::span<const Pixel>    src,
    int                       w,
    int                       h,
    const TriangulateOptions& opts = {});

} // namespace pelpaint::filters
