#pragma once

/** @file
 * @brief BFS flood-fill connected-component labeling on RGBA8 images.
 *
 * Converts a flat RGBA8 ImageView into a dense integer label map where each
 * 4-connected component of same-coloured opaque pixels receives a unique
 * non-negative region id.  Transparent pixels (alpha < alphaThreshold) are
 * left at label -1 and never expanded.
 */

#include "RegionGraph.hpp"    // Vec3 (region average colour)
#include "ExportUtils.hpp"    // ReadPixelRGBA8; transitively: Types.hpp (ImageView, Pixel)

#include <algorithm>
#include <cstdint>
#include <vector>

namespace pelpaint::ir {

/// Dense integer label map produced by LabelFieldBuilder::Build.
///
/// Every pixel (x, y) in the source image maps to:
///   -1  — transparent / background (alpha strictly below threshold)
///   ≥ 0 — zero-based region id
struct LabelField {
    std::vector<int>    labels;       ///< labels[y * width + x] = region id or -1
    std::uint32_t       width      = 0;
    std::uint32_t       height     = 0;
    std::vector<Vec3>   regionColors; ///< [id] average colour in [0, 255] per channel
    std::vector<float>  regionAreas;  ///< [id] pixel count (as float for downstream use)
    int                 numRegions = 0;
};

class LabelFieldBuilder {
public:
    /// Build a LabelField from an RGBA8 ImageView.
    ///
    /// @param view            Source image.  Must have channels >= 4.
    /// @param colorTolerance  Per-channel Chebyshev tolerance for same-region matching.
    ///                        0 = exact match (ideal for hard-edged pixel art).
    ///                        Each candidate neighbour pixel is compared against the
    ///                        seed colour of the region being expanded.
    /// @param alphaThreshold  Pixels with alpha strictly below this are transparent
    ///                        and are excluded from every region.
    ///
    /// @return A fully-populated LabelField, or an empty/default LabelField on error.
    [[nodiscard]]
    static LabelField Build(
        const pelpaint::ImageView& view,
        std::uint8_t               colorTolerance = 0,
        std::uint8_t               alphaThreshold = 10)
    {
        LabelField lf;

        if (!view.valid() || view.data == nullptr || view.channels < 4)
            return lf;

        const std::uint32_t W = view.width;
        const std::uint32_t H = view.height;
        const std::uint32_t N = W * H;

        lf.width  = W;
        lf.height = H;
        lf.labels.assign(static_cast<std::size_t>(N), -1);

        // 1. Cache all pixels into a flat array for O(1) access.
        std::vector<pelpaint::Pixel> pixels(static_cast<std::size_t>(N));
        for (std::uint32_t y = 0; y < H; ++y) {
            for (std::uint32_t x = 0; x < W; ++x) {
                std::uint8_t r = 0, g = 0, b = 0, a = 0;
                pelpaint::exporter::ReadPixelRGBA8(view, x, y, r, g, b, a);
                pixels[y * W + x] = {r, g, b, a};
            }
        }

        // 2. BFS labeling.
        // Per-region running colour sums (deferred average to avoid fp accumulation)
        struct ColorAcc {
            std::uint64_t rSum  = 0;
            std::uint64_t gSum  = 0;
            std::uint64_t bSum  = 0;
            std::uint64_t count = 0;
        };
        std::vector<ColorAcc> accumulators;

        // Reusable BFS queue walked with a head index — avoids O(n) front-pop.
        std::vector<int> queue;
        queue.reserve(std::min(static_cast<std::uint32_t>(65536u), N));

        // 4-connectivity neighbour deltas
        constexpr int kDx[4] = {  0, 1, 0, -1 };
        constexpr int kDy[4] = { -1, 0, 1,  0 };

        // Returns true when pixel `b` matches seed `a` within colorTolerance
        auto colorsMatch = [&](const pelpaint::Pixel& a,
                               const pelpaint::Pixel& b) noexcept -> bool
        {
            const int tol = static_cast<int>(colorTolerance);
            auto absdiff = [](std::uint8_t u, std::uint8_t v) -> int {
                return std::abs(static_cast<int>(u) - static_cast<int>(v));
            };
            return absdiff(a.r, b.r) <= tol
                && absdiff(a.g, b.g) <= tol
                && absdiff(a.b, b.b) <= tol;
        };

        for (std::uint32_t y = 0; y < H; ++y) {
            for (std::uint32_t x = 0; x < W; ++x) {
                const std::uint32_t seedIdx = y * W + x;

                if (lf.labels[seedIdx] != -1) continue;            // already labeled

                const pelpaint::Pixel& seed = pixels[seedIdx];
                if (seed.a < alphaThreshold) continue;             // transparent

                // Start a new region
                const int regionId = lf.numRegions++;
                accumulators.push_back({});
                ColorAcc& acc = accumulators.back();

                lf.labels[seedIdx] = regionId;
                queue.clear();
                queue.push_back(static_cast<int>(seedIdx));

                // Walk queue with a head-index cursor
                for (int qHead = 0; qHead < static_cast<int>(queue.size()); ++qHead) {
                    const int            curIdx = queue[qHead];
                    const std::uint32_t  cx     = static_cast<std::uint32_t>(curIdx) % W;
                    const std::uint32_t  cy     = static_cast<std::uint32_t>(curIdx) / W;
                    const pelpaint::Pixel& cp   = pixels[static_cast<std::size_t>(curIdx)];

                    // Accumulate colour sums for this pixel
                    acc.rSum  += cp.r;
                    acc.gSum  += cp.g;
                    acc.bSum  += cp.b;
                    acc.count += 1;

                    // Expand to 4-connected neighbours
                    for (int d = 0; d < 4; ++d) {
                        const int nx = static_cast<int>(cx) + kDx[d];
                        const int ny = static_cast<int>(cy) + kDy[d];

                        if (nx < 0 || ny < 0 ||
                            nx >= static_cast<int>(W) ||
                            ny >= static_cast<int>(H)) continue;

                        const std::uint32_t nIdx =
                            static_cast<std::uint32_t>(ny) * W +
                            static_cast<std::uint32_t>(nx);

                        if (lf.labels[nIdx] != -1) continue;       // already visited

                        const pelpaint::Pixel& np = pixels[nIdx];
                        if (np.a < alphaThreshold) continue;        // transparent
                        if (!colorsMatch(seed, np)) continue;       // colour mismatch

                        lf.labels[nIdx] = regionId;
                        queue.push_back(static_cast<int>(nIdx));
                    }
                }
            }
        }

        // 3. Compute per-region average colours and areas.
        lf.regionColors.resize(static_cast<std::size_t>(lf.numRegions));
        lf.regionAreas.resize(static_cast<std::size_t>(lf.numRegions), 0.f);

        for (int id = 0; id < lf.numRegions; ++id) {
            const ColorAcc& acc = accumulators[static_cast<std::size_t>(id)];
            if (acc.count > 0) {
                const float inv = 1.f / static_cast<float>(acc.count);
                lf.regionColors[static_cast<std::size_t>(id)] = {
                    static_cast<float>(acc.rSum) * inv,
                    static_cast<float>(acc.gSum) * inv,
                    static_cast<float>(acc.bSum) * inv
                };
            }
            lf.regionAreas[static_cast<std::size_t>(id)] =
                static_cast<float>(acc.count);
        }

        return lf;
    }
};

} // namespace pelpaint::ir
