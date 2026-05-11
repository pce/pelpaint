#pragma once

/** @file @brief Error-diffusion and ordered dithering algorithms. */

#include <span>

#include "colorspace.hpp"   // FilterResult + find_nearest (used in impls)

namespace pelpaint::filters {

/// Floyd-Steinberg error-diffusion (7 3 5 1 / 16 kernel).
/// @param palette       Target palette — must not be empty.
/// @param preserve_alpha Copy source alpha verbatim into the output.
[[nodiscard]] FilterResult floyd_steinberg(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    bool                   preserve_alpha = false);

/// Atkinson error-diffusion (1/8-weight kernel, distributes 6/8 of the error).
[[nodiscard]] FilterResult atkinson(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    bool                   preserve_alpha = false);

/// Stucki error-diffusion (wide-kernel, denominator 42).
[[nodiscard]] FilterResult stucki(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    bool                   preserve_alpha = false);

/// Bayer 4×4 ordered (threshold-matrix) dithering.
[[nodiscard]] FilterResult ordered_dithering(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    bool                   preserve_alpha = false);


/// Canonical resolution presets for the dithering frame.
///
/// @c Native (0) means: use the image's actual pixel dimensions.
/// Any other value means: downscale the longest edge to at most that many
/// pixels (aspect-preserving), dither at that resolution, then upscale
/// back to the original dimensions via nearest-neighbour.
///
/// This makes the dithering pattern portable: a 4 K image dithered at R256
/// produces the same coarse pixel-art look as a 256-pixel thumbnail, and
/// both scale cleanly to any display size.
enum class DitherResolution : int {
    Native = 0,
    R128   = 128,
    R256   = 256,
    R512   = 512,
    R1K    = 1024,
    R2K    = 2048,
    R4K    = 4096,
    R8K    = 8192,
};

/// Options for the Dithering pre/post-pass pipeline.
struct DitherOptions {
    /// Target frame resolution.
    /// When set to anything other than Native, and when the source image is
    /// larger than this value, the image is downscaled to fit within a square
    /// of this dimension (aspect-preserving) before dithering, then upscaled
    /// back to the original dimensions afterward.
    DitherResolution frameResolution = DitherResolution::Native;

    /// Pre-pass: linearly stretch the per-channel histogram so the darkest
    /// pixel maps to 0 and the brightest pixel maps to 255 before dithering.
    /// Useful for images with low contrast — brings out more dither detail.
    bool preNormalize = false;

    /// Pixel density multiplier applied on top of @c frameResolution.
    /// 1.0 = no change.  0.5 = halve the working resolution (coarser dither
    /// pattern, larger "pixel blocks" in the output).  Values > 1.0 are
    /// clamped to 1.0 (no upsampling before dithering).
    float pixelDensity = 1.0f;

    /// Post-pass: nearest-neighbour upscale back to the original image
    /// dimensions after dithering.  Ignored when neither @c frameResolution
    /// nor @c pixelDensity causes a working-resolution change.
    bool postNearestScale = true;
};

/// Function-pointer type matching all four dithering entry points.
using DitheringFn = FilterResult(*)(
    std::span<const Pixel>, int, int, std::span<const Pixel>, bool);

/// Pre-pass helper: linearly stretch per-channel histogram to [0, 255].
/// Alpha is copied verbatim.  Returns src unchanged if the image is empty
/// or if any channel has zero range (all pixels the same value).
[[nodiscard]] std::vector<Pixel> normalize_contrast(
    std::span<const Pixel> src);

/// Nearest-neighbour resampler.
/// Returns a new pixel buffer of size @p dstW × @p dstH.
[[nodiscard]] std::vector<Pixel> scale_nearest(
    std::span<const Pixel> src,
    int srcW, int srcH,
    int dstW, int dstH);

/// Chains:
///   1. (optional) normalize_contrast        — pre-pass histogram stretch
///   2. (optional) scale_nearest downscale   — frame resolution / pixel density
///   3. @p fn                                — core dithering at working res
///   4. (optional) scale_nearest upscale     — back to original dimensions
///
/// Pass any of @c floyd_steinberg, @c atkinson, @c stucki, or
/// @c ordered_dithering as @p fn.
[[nodiscard]] FilterResult apply_dither_pipeline(
    std::span<const Pixel> src,
    int                    w,
    int                    h,
    std::span<const Pixel> palette,
    DitheringFn            fn,
    const DitherOptions&   opts          = {},
    bool                   preserve_alpha = false);

} // namespace pelpaint::filters
