#pragma once

#include "ExportUtils.hpp"
#include "../core/Types.hpp"

#include <cstdint>
#include <vector>

namespace pelpaint::exporter {


struct ScanRun {
    std::uint32_t x     = 0;   ///< left pixel (column)
    std::uint32_t y     = 0;   ///< source row
    std::uint32_t width = 0;   ///< number of pixels in the run (≥ 1)
    std::uint8_t  r = 0, g = 0, b = 0, a = 0;

    [[nodiscard]] bool SameSpan(const ScanRun& o) const noexcept {
        return x == o.x && width == o.width
            && r == o.r && g == o.g && b == o.b && a == o.a;
    }
};


struct ScanRect {
    std::uint32_t x = 0;   ///< left column (pixels)
    std::uint32_t y = 0;   ///< top row (pixels)
    std::uint32_t w = 0;   ///< width in pixels
    std::uint32_t h = 0;   ///< height in pixels
    std::uint8_t  r = 0, g = 0, b = 0, a = 0;
};

/// @brief Two-pass scanline merge for pixel art.
///
/// @details
/// Pass 1 — Horizontal RLE:
///   Each row is run-length encoded into same-colour horizontal spans.
///   Adjacent pixels with the same RGBA value are fused into a single
///   ScanRun entry.  Transparent pixels (alpha < threshold) are skipped.
///
///   Row 0:  [R][R][R][G][G]  →  {x=0, w=3, R}  {x=3, w=2, G}
///
/// Pass 2 — Vertical stack merge:
///   Consecutive rows that share the exact same run at the same (x, w)
///   are merged vertically into one rectangle.
///
///   Row 0:  {x=0, w=3, R}
///   Row 1:  {x=0, w=3, R}   ← same → merge
///   Row 2:  {x=0, w=2, R}   ← different width → flush row 0-1 rect
///
/// This mirrors the "sum-up cells" intuition from a spreadsheet:
///
///   [ ][ ][ ]  ← 3 horiz cells merged
///   [ ][ ][ ]  ← same row below → stack vertically
class ScanlineMerger {
public:
    /// Merge all opaque pixels in `view` into a compact set of
    /// axis-aligned rectangles.  Pixels with alpha < alphaThreshold
    /// are treated as transparent and are not included in the output.
    [[nodiscard]]
    static std::vector<ScanRect> Merge(
        const pelpaint::ImageView& view,
        std::uint8_t               alphaThreshold = 10)
    {
        if (!view.valid() || !view.data || view.channels < 4) return {};

        const std::uint32_t W = view.width;
        const std::uint32_t H = view.height;

        // Pass 1: RLE each row
        std::vector<std::vector<ScanRun>> rowRuns(H);
        for (std::uint32_t y = 0; y < H; ++y)
            rowRuns[y] = HorizontalRLE(view, y, alphaThreshold);

        // Pass 2: stack rows
        return VerticalStack(rowRuns, H);
    }

    /// Pass 1: run-length encode a single row into horizontal spans.
    [[nodiscard]]
    static std::vector<ScanRun> HorizontalRLE(
        const pelpaint::ImageView& view,
        std::uint32_t              row,
        std::uint8_t               alphaThreshold = 10)
    {
        std::vector<ScanRun> runs;
        const std::uint32_t W = view.width;
        std::uint32_t x = 0;

        while (x < W) {
            std::uint8_t r, g, b, a;
            ReadPixelRGBA8(view, x, row, r, g, b, a);

            if (a < alphaThreshold) { ++x; continue; }

            ScanRun run{ x, row, 1u, r, g, b, a };

            while (x + run.width < W) {
                std::uint8_t nr, ng, nb, na;
                ReadPixelRGBA8(view, x + run.width, row, nr, ng, nb, na);
                if (nr != r || ng != g || nb != b || na != a) break;
                ++run.width;
            }

            runs.push_back(run);
            x += run.width;
        }
        return runs;
    }

    /// Pass 2: stack same-span runs from consecutive rows into rectangles.
    ///
    /// A "same span" means identical (x, width, r, g, b, a).
    /// As soon as a span breaks or the row changes it, the accumulator
    /// is flushed as a completed ScanRect.
    [[nodiscard]]
    static std::vector<ScanRect> VerticalStack(
        const std::vector<std::vector<ScanRun>>& rowRuns,
        std::uint32_t                            totalRows)
    {
        std::vector<ScanRect> result;

        // Active accumulators: one per distinct span currently being extended.
        struct Accum {
            ScanRun   run;       ///< span spec (x, w, colour)
            std::uint32_t startY; ///< first row of this rect
            std::uint32_t height; ///< rows accumulated so far
        };
        std::vector<Accum> active;

        for (std::uint32_t y = 0; y < totalRows; ++y) {
            const auto& runs = rowRuns[y];

            // Temporary: which active accumulators survive this row?
            std::vector<bool> survived(active.size(), false);
            std::vector<bool> runUsed(runs.size(), false);

            // Try to extend each active accumulator with a matching run.
            for (std::size_t ai = 0; ai < active.size(); ++ai) {
                for (std::size_t ri = 0; ri < runs.size(); ++ri) {
                    if (!runUsed[ri] && runs[ri].SameSpan(active[ai].run)) {
                        ++active[ai].height;
                        survived[ai] = true;
                        runUsed[ri]  = true;
                        break;
                    }
                }
            }

            // Flush any accumulator that did NOT survive.
            std::vector<Accum> nextActive;
            for (std::size_t ai = 0; ai < active.size(); ++ai) {
                if (survived[ai]) {
                    nextActive.push_back(active[ai]);
                } else {
                    const auto& a = active[ai];
                    result.push_back(ScanRect{
                        a.run.x, a.startY, a.run.width, a.height,
                        a.run.r, a.run.g, a.run.b, a.run.a
                    });
                }
            }

            // Open new accumulators for runs that weren't matched.
            for (std::size_t ri = 0; ri < runs.size(); ++ri) {
                if (!runUsed[ri]) {
                    nextActive.push_back(Accum{ runs[ri], y, 1u });
                }
            }

            active = std::move(nextActive);
        }

        // Flush all remaining accumulators.
        for (const auto& a : active) {
            result.push_back(ScanRect{
                a.run.x, a.startY, a.run.width, a.height,
                a.run.r, a.run.g, a.run.b, a.run.a
            });
        }

        return result;
    }
};

} // namespace pelpaint::exporter
