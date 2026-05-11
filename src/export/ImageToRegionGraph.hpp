#pragma once

#include "RegionGraph.hpp"    // pelpaint::ir::{RegionGraph, Region, Vec2, AABB, Vec3}
#include "LabelField.hpp"     // pelpaint::ir::{LabelField, LabelFieldBuilder}
#include "ContourTrace.hpp"   // pelpaint::ir::ContourTrace
#include "RDP.hpp"            // pelpaint::ir::RDP::SimplifyPolygon
#include "../core/Types.hpp"  // pelpaint::ImageView

#include <algorithm>          // std::min, std::max
#include <cstdint>            // uint8_t, uint32_t
#include <set>                // std::set
#include <utility>            // std::pair, std::move
#include <vector>             // std::vector

namespace pelpaint::exporter {

struct RegionGraphOptions {
    uint8_t colorTolerance = 0;     ///< 0 = exact match (pixel art); >0 = fuzzy flood-fill
    uint8_t alphaThreshold = 10;    ///< pixels with alpha < this are treated as transparent
    float   rdpEpsilon     = 0.5f;  ///< RDP contour simplification; 0 = collinear-only
    int     minArea        = 1;     ///< skip regions smaller than this many pixels
};

class ImageToRegionGraph {
public:
    /// Convert a raw RGBA image into a RegionGraph.
    ///
    /// The returned graph contains:
    ///   • One Region per connected component that passes all filters.
    ///   • Adjacency pairs (ordered min < max) for all 4-connected
    ///     region boundaries found in the label field — including
    ///     boundaries involving regions that were filtered out.
    ///   • canvasW / canvasH set to view.width / view.height.
    ///
    /// Returns an empty graph (no regions, no adjacency) if `view`
    /// is not valid.
    [[nodiscard]]
    static pelpaint::ir::RegionGraph Build(
        const pelpaint::ImageView& view,
        const RegionGraphOptions&  opts = {})
    {
        pelpaint::ir::RegionGraph graph;
        if (!view.valid()) return graph;

        // Step 1: Flood-fill connected components.
        pelpaint::ir::LabelField lf =
            pelpaint::ir::LabelFieldBuilder::Build(
                view,
                opts.colorTolerance,
                opts.alphaThreshold);

        const uint32_t W = lf.width;
        const uint32_t H = lf.height;

        // Steps 2–5: Trace + simplify + assemble regions.
        graph.regions.reserve(static_cast<std::size_t>(lf.numRegions));

        for (int id = 0; id < lf.numRegions; ++id) {

            // a) Area filter — skip tiny regions
            if (lf.regionAreas[static_cast<std::size_t>(id)] <
                static_cast<float>(opts.minArea))
                continue;

            // b) Trace the raw boundary polygon
            std::vector<pelpaint::ir::Vec2> rawContour =
                pelpaint::ir::ContourTrace::Trace(lf, id);
            if (rawContour.size() < 3) continue;

            // c,d) Simplify with Ramer-Douglas-Peucker
            std::vector<pelpaint::ir::Vec2> contour =
                pelpaint::ir::RDP::SimplifyPolygon(rawContour, opts.rdpEpsilon);

            // e) Re-check after simplification
            if (contour.size() < 3) continue;

            // f) Compute axis-aligned bounding box from contour vertices
            float minX = contour[0].x, maxX = contour[0].x;
            float minY = contour[0].y, maxY = contour[0].y;
            for (const auto& p : contour) {
                if (p.x < minX) minX = p.x;
                if (p.x > maxX) maxX = p.x;
                if (p.y < minY) minY = p.y;
                if (p.y > maxY) maxY = p.y;
            }
            const pelpaint::ir::AABB bbox {
                minX, minY,
                maxX - minX,
                maxY - minY
            };

            // g) Assemble the Region
            pelpaint::ir::Region region;
            region.id      = id;
            region.contour = std::move(contour);
            region.color   = lf.regionColors[static_cast<std::size_t>(id)];
            region.bbox    = bbox;
            region.area    = lf.regionAreas[static_cast<std::size_t>(id)];

            // h) Push into the graph
            graph.regions.push_back(std::move(region));
        }

        // Step 6: Build adjacency from the label field.
        //
        // Scan every pixel; for each horizontal / vertical neighbour
        // pair with distinct non-negative region IDs, add an ordered
        // pair {min(a,b), max(a,b)} to an intermediate set (which
        // deduplicates automatically) and then copy into the graph.
        {
            std::set<std::pair<int,int>> adjSet;

            for (uint32_t y = 0; y < H; ++y) {
                for (uint32_t x = 0; x < W; ++x) {
                    const int id = lf.labels[y * W + x];
                    if (id < 0) continue;

                    // Check right neighbour
                    if (x + 1u < W) {
                        const int rid = lf.labels[y * W + x + 1u];
                        if (rid >= 0 && rid != id)
                            adjSet.insert({ std::min(id, rid),
                                            std::max(id, rid) });
                    }

                    // Check bottom neighbour
                    if (y + 1u < H) {
                        const int did = lf.labels[(y + 1u) * W + x];
                        if (did >= 0 && did != id)
                            adjSet.insert({ std::min(id, did),
                                            std::max(id, did) });
                    }
                }
            }

            graph.adjacency.assign(adjSet.begin(), adjSet.end());
        }

        // Step 7: Record canvas dimensions.
        graph.canvasW = view.width;
        graph.canvasH = view.height;

        return graph;
    }
};

} // namespace pelpaint::exporter
