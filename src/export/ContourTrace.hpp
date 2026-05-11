#pragma once

/** @file
 * @brief Pixel-boundary half-edge contour extractor.
 */

#include "LabelField.hpp"    // LabelField; Vec2 via RegionGraph.hpp

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace pelpaint::ir {

class ContourTrace {
public:
    /// Extract the ordered outer boundary polygon for one region.
    ///
    /// The chain is started at the topmost (minimum y), then leftmost
    /// (minimum x) start point among all half-edges.  For convex or
    /// simply-connected regions this yields the complete outer boundary.
    ///
    /// @par Coordinate system
    /// Contour vertices are pixel-grid *corner* coordinates.  A pixel at
    /// column x, row y occupies the unit square with top-left corner (x, y)
    /// and bottom-right corner (x+1, y+1).  All coordinates are non-negative
    /// integers stored as float.  The outer boundary is wound clockwise
    /// (y axis pointing downward, as in image/SVG space).
    ///
    /// Half-edge direction rules for pixel (x, y):
    ///   North side (row y-1 is different): (x,   y)   → (x+1, y)
    ///   East  side (col x+1 is different): (x+1, y)   → (x+1, y+1)
    ///   South side (row y+1 is different): (x+1, y+1) → (x,   y+1)
    ///   West  side (col x-1 is different): (x,   y+1) → (x,   y)
    ///
    /// @return Ordered list of polygon corner vertices (no repeated first vertex).
    ///         Returns an empty vector if regionId is out of range or the
    ///         region has no boundary half-edges.
    [[nodiscard]]
    static std::vector<Vec2> Trace(const LabelField& lf, int regionId)
    {
        if (lf.width == 0 || lf.height == 0) return {};
        if (regionId < 0 || regionId >= lf.numRegions) return {};

        const std::uint32_t W = lf.width;
        const std::uint32_t H = lf.height;

        // 1. Collect directed half-edges.
        //
        // Each edge marks one side of one pixel that borders a different
        // region or the image boundary.
        struct Edge { Vec2 start, end; };
        std::vector<Edge> edges;
        edges.reserve(512);

        // Returns the label at (x, y), or -2 (≠ any valid region id) if
        // (x, y) is outside the image.
        auto labelAt = [&](int x, int y) noexcept -> int {
            if (x < 0 || y < 0 ||
                x >= static_cast<int>(W) ||
                y >= static_cast<int>(H)) return -2;
            return lf.labels[static_cast<std::uint32_t>(y) * W +
                              static_cast<std::uint32_t>(x)];
        };

        for (std::uint32_t y = 0; y < H; ++y) {
            for (std::uint32_t x = 0; x < W; ++x) {
                if (lf.labels[y * W + x] != regionId) continue;

                const float fx = static_cast<float>(x);
                const float fy = static_cast<float>(y);
                const int   ix = static_cast<int>(x);
                const int   iy = static_cast<int>(y);

                // North side: row above is a different region / out of bounds
                if (labelAt(ix, iy - 1) != regionId)
                    edges.push_back({{ fx,       fy       }, { fx + 1.f, fy       }});

                // East side:  col to the right is different / out of bounds
                if (labelAt(ix + 1, iy) != regionId)
                    edges.push_back({{ fx + 1.f, fy       }, { fx + 1.f, fy + 1.f }});

                // South side: row below is different / out of bounds
                if (labelAt(ix, iy + 1) != regionId)
                    edges.push_back({{ fx + 1.f, fy + 1.f }, { fx,       fy + 1.f }});

                // West side:  col to the left is different / out of bounds
                if (labelAt(ix - 1, iy) != regionId)
                    edges.push_back({{ fx,       fy + 1.f }, { fx,       fy       }});
            }
        }

        if (edges.empty()) return {};

        // 2. Build start-point → edge-index lookup.
        // Key encoding: pack (uint32_t x, uint32_t y) into a uint64_t.
        //   key = (uint32_t(p.x) << 32) | uint32_t(p.y)
        // All boundary coordinates are non-negative integers (pixel-grid
        // corners), so the casts are safe for images up to ~4 billion
        // pixels per axis.  No custom hash struct needed — plain uint64_t.
        auto encodeKey = [](const Vec2& p) noexcept -> std::uint64_t {
            return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.x)) << 32)
                 |  static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.y));
        };

        std::unordered_map<std::uint64_t, int> startMap;
        startMap.reserve(edges.size());
        for (int i = 0; i < static_cast<int>(edges.size()); ++i)
            startMap.emplace(encodeKey(edges[i].start), i);

        // 3. Find the topmost-then-leftmost starting edge.
        // "Topmost" = minimum y (screen space, y increases downward).
        // Ties broken by minimum x.  Starting here guarantees we enter
        // the outer boundary rather than an interior hole.
        int startEdgeIdx = 0;
        for (int i = 1; i < static_cast<int>(edges.size()); ++i) {
            const Vec2& best = edges[startEdgeIdx].start;
            const Vec2& cand = edges[i].start;
            if (cand.y < best.y || (cand.y == best.y && cand.x < best.x))
                startEdgeIdx = i;
        }

        // 4. Walk the chain.
        // Follow: current edge's end → look up next edge whose start
        // matches → append next start → repeat until we return to the
        // starting edge index.
        // The safety cap (result.size() < limit) prevents an infinite
        // loop if the input is degenerate (broken chain).
        std::vector<Vec2> result;
        result.reserve(edges.size());

        const std::size_t limit = edges.size();
        int curIdx = startEdgeIdx;

        do {
            result.push_back(edges[curIdx].start);

            const auto it = startMap.find(encodeKey(edges[curIdx].end));
            if (it == startMap.end()) break;   // chain broken — degenerate region

            curIdx = it->second;

        } while (curIdx != startEdgeIdx && result.size() < limit);

        return result;
    }
};

} // namespace pelpaint::ir
