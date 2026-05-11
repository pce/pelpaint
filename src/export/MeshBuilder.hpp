#pragma once

/** @file
 * @brief Builds 3-D extruded meshes from RegionGraph regions.
 */

#include "RegionGraph.hpp"   // Mesh, Vertex, Vec2, Vec3
#include "EarClip.hpp"

#include <cstdint>
#include <vector>
#include <cmath>

namespace pelpaint::ir {

struct ExtrudeOptions {
    float depth         = 1.0f;   ///< extrusion height in world units
    bool  symmetric     = false;  ///< true → ±depth/2; false → 0 to depth
    bool  flipWindingForBottom = true; ///< bottom face wound opposite to top
    float uvScale       = 1.0f;   ///< UV = pixel coord / uvScale
};

class MeshBuilder {
public:
    /**
     * @brief Build an extruded mesh for one region.
     *
     * Contour coordinates are assumed to be in pixel space;
     * the caller is responsible for any normalisation (divide by canvas dim).
     *
     * @note When `opts.symmetric` is true the slab is centred on z = 0:
     *   top face at z = +depth/2, bottom face at z = -depth/2.
     *   This produces pixel-perfect cubes when the region bbox equals depth.
     */
    [[nodiscard]]
    static Mesh Build(
        const Region&         region,
        const ExtrudeOptions& opts = {})
    {
        Mesh m;
        if (!region.valid()) return m;

        const auto& poly = region.contour;
        const int   n    = static_cast<int>(poly.size());

        // Z bounds
        const float zTop = opts.symmetric ? +opts.depth * 0.5f : opts.depth;
        const float zBot = opts.symmetric ? -opts.depth * 0.5f : 0.0f;

        const std::uint32_t topBase = static_cast<std::uint32_t>(m.vertices.size());
        for (const auto& p : poly) {
            Vertex v;
            v.pos    = { p.x, p.y, zTop };
            v.normal = { 0.f, 0.f, 1.f };     // +Z up
            v.uv     = { p.x / opts.uvScale, p.y / opts.uvScale };
            m.vertices.push_back(v);
        }
        auto topTris = EarClip::Triangulate(poly);
        for (auto i : topTris)
            m.indices.push_back(topBase + i);

        const std::uint32_t botBase = static_cast<std::uint32_t>(m.vertices.size());
        for (const auto& p : poly) {
            Vertex v;
            v.pos    = { p.x, p.y, zBot };
            v.normal = { 0.f, 0.f, -1.f };    // -Z down
            v.uv     = { p.x / opts.uvScale, p.y / opts.uvScale };
            m.vertices.push_back(v);
        }
        if (opts.flipWindingForBottom) {
            // Reverse each triangle's winding
            for (std::size_t i = 0; i + 2 < topTris.size(); i += 3) {
                m.indices.push_back(botBase + topTris[i    ]);
                m.indices.push_back(botBase + topTris[i + 2]);
                m.indices.push_back(botBase + topTris[i + 1]);
            }
        } else {
            for (auto i : topTris)
                m.indices.push_back(botBase + i);
        }

        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;

            const Vec2& p0 = poly[i];
            const Vec2& p1 = poly[j];

            // Outward-facing normal (perpendicular to the edge, pointing outward)
            const Vec2  edge  { p1.x - p0.x, p1.y - p0.y };
            const float elen  = edge.len();
            const Vec3  nrm   = (elen > 1e-7f)
                                ? Vec3{ edge.y / elen, -edge.x / elen, 0.f }
                                : Vec3{ 1.f, 0.f, 0.f };

            // Four quad corners: top-left, top-right, bot-right, bot-left
            const std::uint32_t base = static_cast<std::uint32_t>(m.vertices.size());
            m.vertices.push_back(Vertex{{ p0.x, p0.y, zTop }, nrm,
                                        { 0.f,                0.f }});
            m.vertices.push_back(Vertex{{ p1.x, p1.y, zTop }, nrm,
                                        { elen / opts.uvScale, 0.f }});
            m.vertices.push_back(Vertex{{ p1.x, p1.y, zBot }, nrm,
                                        { elen / opts.uvScale, opts.depth / opts.uvScale }});
            m.vertices.push_back(Vertex{{ p0.x, p0.y, zBot }, nrm,
                                        { 0.f,                opts.depth / opts.uvScale }});

            // Two triangles (CCW from outside)
            m.indices.push_back(base + 0);
            m.indices.push_back(base + 1);
            m.indices.push_back(base + 2);
            m.indices.push_back(base + 0);
            m.indices.push_back(base + 2);
            m.indices.push_back(base + 3);
        }

        return m;
    }

    /// Build and concatenate extruded meshes for every region in the graph.
    /// All meshes share one index space (vertex offsets are adjusted).
    [[nodiscard]]
    static Mesh BuildAll(
        const RegionGraph&    graph,
        const ExtrudeOptions& opts = {})
    {
        Mesh combined;
        for (const auto& r : graph.regions) {
            if (!r.valid()) continue;
            const std::uint32_t vOffset =
                static_cast<std::uint32_t>(combined.vertices.size());

            Mesh part = Build(r, opts);
            for (const auto& v : part.vertices)
                combined.vertices.push_back(v);
            for (auto idx : part.indices)
                combined.indices.push_back(vOffset + idx);
        }
        combined.recomputeNormals();
        return combined;
    }

    /// Normalise a pixel-space contour to [0, 1] range.
    ///
    /// @note Must be called before Build() for GLTF unit-scale output.
    [[nodiscard]]
    static std::vector<Vec2> NormaliseContour(
        const std::vector<Vec2>& contour,
        float                    canvasMaxDim)
    {
        if (canvasMaxDim < 1.f) return contour;
        const float inv = 1.f / canvasMaxDim;
        std::vector<Vec2> out;
        out.reserve(contour.size());
        for (const auto& p : contour)
            out.push_back({ p.x * inv, p.y * inv });
        return out;
    }
};

} // namespace pelpaint::ir
