#pragma once

/** @file
 * @brief Intermediate representation (IR) for the image → geometry pipeline.
 */

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace pelpaint::ir {

struct Vec2 {
    float x = 0.f, y = 0.f;
    [[nodiscard]] Vec2 operator+(const Vec2& o) const noexcept { return {x+o.x, y+o.y}; }
    [[nodiscard]] Vec2 operator-(const Vec2& o) const noexcept { return {x-o.x, y-o.y}; }
    [[nodiscard]] Vec2 operator*(float s)        const noexcept { return {x*s,   y*s};   }
    [[nodiscard]] float dot(const Vec2& o)        const noexcept { return x*o.x + y*o.y; }
    [[nodiscard]] float cross(const Vec2& o)      const noexcept { return x*o.y - y*o.x; }
    [[nodiscard]] float len()                     const noexcept { return std::sqrt(x*x+y*y); }
    [[nodiscard]] bool operator==(const Vec2&)    const noexcept = default;
};

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
    [[nodiscard]] Vec3 operator+(const Vec3& o) const noexcept { return {x+o.x, y+o.y, z+o.z}; }
    [[nodiscard]] Vec3 operator-(const Vec3& o) const noexcept { return {x-o.x, y-o.y, z-o.z}; }
    [[nodiscard]] Vec3 operator*(float s)        const noexcept { return {x*s,   y*s,   z*s};   }
    [[nodiscard]] float dot(const Vec3& o)        const noexcept { return x*o.x+y*o.y+z*o.z; }
    [[nodiscard]] Vec3 cross(const Vec3& o)       const noexcept {
        return { y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x };
    }
    [[nodiscard]] float len()                     const noexcept { return std::sqrt(x*x+y*y+z*z); }
    [[nodiscard]] Vec3  normalized()              const noexcept {
        const float l = len();
        return l > 1e-7f ? (*this)*(1.f/l) : Vec3{};
    }
};

struct AABB {
    float x = 0.f;   ///< left
    float y = 0.f;   ///< top
    float w = 0.f;   ///< width
    float h = 0.f;   ///< height

    [[nodiscard]] float right()  const noexcept { return x + w; }
    [[nodiscard]] float bottom() const noexcept { return y + h; }
    [[nodiscard]] Vec2  center() const noexcept { return {x + w*0.5f, y + h*0.5f}; }
};

struct Vertex {
    Vec3 pos;
    Vec3 normal;     ///< unit normal (flat-shaded per triangle)
    Vec2 uv;         ///< texture coordinates [0,1]
};

struct Mesh {
    std::vector<Vertex>            vertices;
    std::vector<std::uint32_t>     indices;     ///< triangle list (3 indices per tri)

    void clear() { vertices.clear(); indices.clear(); }
    [[nodiscard]] bool empty() const noexcept { return vertices.empty(); }

    /// Recompute flat normals from triangle geometry.
    void recomputeNormals()
    {
        for (auto& v : vertices) v.normal = {};
        const std::size_t n = indices.size();
        for (std::size_t i = 0; i + 2 < n; i += 3) {
            Vertex& v0 = vertices[indices[i    ]];
            Vertex& v1 = vertices[indices[i + 1]];
            Vertex& v2 = vertices[indices[i + 2]];
            const Vec3 edge1 = v1.pos - v0.pos;
            const Vec3 edge2 = v2.pos - v0.pos;
            const Vec3 fn    = edge1.cross(edge2).normalized();
            v0.normal = v1.normal = v2.normal = fn;
        }
    }
};

/// One connected region of uniform colour in the source image.
struct Region {
    int                id      = -1;

    /// Ordered boundary polygon (pixel-grid corners after RDP simplification).
    /// Coordinates are in image-pixel space: [0, image_width] × [0, image_height].
    std::vector<Vec2>  contour;

    /// Representative colour in [0, 255] per channel.
    Vec3               color;

    /// Axis-aligned bounding box (pixel space).
    AABB               bbox;

    /// Number of source pixels covered by this region.
    float              area = 0.f;

    [[nodiscard]] bool valid() const noexcept {
        return id >= 0 && contour.size() >= 3;
    }
};

/**
 * @brief Central IR node produced by LabelField and consumed by all exporters.
 *
 * Pipeline:
 *   ImageView
 *     -> LabelField          (flood-fill connected components)
 *     -> RegionGraph         (contour + colour + adjacency)
 *     -> SvgRegionExporter   (<path> per region)
 *     -> GltfExporter        (extruded 3D mesh)
 *     -> MeshExporter        (PLY fallback)
 */
struct RegionGraph {
    std::vector<Region>                regions;
    std::vector<std::pair<int,int>>    adjacency;  ///< (id_a, id_b) — undirected

    /// Canvas dimensions in pixels.
    std::uint32_t  canvasW = 0;
    std::uint32_t  canvasH = 0;

    [[nodiscard]] const Region* findRegion(int id) const noexcept {
        for (const auto& r : regions)
            if (r.id == id) return &r;
        return nullptr;
    }

    [[nodiscard]] bool areAdjacent(int a, int b) const noexcept {
        for (const auto& [x, y] : adjacency)
            if ((x == a && y == b) || (x == b && y == a)) return true;
        return false;
    }

    /// Compute the AABB of all regions (= canvas bounds in normalized space).
    [[nodiscard]] AABB totalBounds() const noexcept {
        return { 0.f, 0.f,
                 static_cast<float>(canvasW),
                 static_cast<float>(canvasH) };
    }
};

} // namespace pelpaint::ir
