#pragma once

/** @file
 * @brief Ear-clipping polygon triangulation — no external dependencies.
 */

#include "RegionGraph.hpp"   // Vec2
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace pelpaint::ir {

/**
 * @brief Triangulates a simple polygon (no self-intersections, no holes) via ear-clipping.
 *
 * Accepts CW or CCW winding; normalises to CCW internally. Complexity is O(n²),
 * which is fast enough for the simplified contours produced by the RDP pass
 * (typically < 200 vertices for pixel art).
 *
 * @see Meister (1975), "Polygons have ears"
 * @see Eberly, "Triangulation by Ear Clipping" (Geometric Tools)
 */
class EarClip {
public:
    /// Triangulate a simple polygon given as an ordered list of 2D vertices.
    ///
    /// Returns a flat list of indices (triplets) into `poly`.
    /// Returns an empty list if the polygon has fewer than 3 vertices.
    /// Winding may be CW or CCW — the function normalises internally.
    [[nodiscard]]
    static std::vector<std::uint32_t> Triangulate(
        const std::vector<Vec2>& poly)
    {
        const int n = static_cast<int>(poly.size());
        if (n < 3) return {};
        if (n == 3) return { 0u, 1u, 2u };

        // Build mutable index list (will shrink as ears are clipped)
        std::vector<int> idx(n);
        std::iota(idx.begin(), idx.end(), 0);

        // Ensure CCW winding for the algorithm
        if (signedArea(poly, idx) < 0.f)
            std::reverse(idx.begin(), idx.end());

        std::vector<std::uint32_t> result;
        result.reserve(static_cast<std::size_t>(n - 2) * 3u);

        int safety = n * n;   // prevent infinite loops on degenerate input
        while (idx.size() > 3 && safety-- > 0) {
            const int m = static_cast<int>(idx.size());
            bool clipped = false;

            for (int i = 0; i < m; ++i) {
                const int prev = (i - 1 + m) % m;
                const int next = (i + 1)      % m;

                const Vec2& A = poly[idx[prev]];
                const Vec2& B = poly[idx[i   ]];
                const Vec2& C = poly[idx[next]];

                // B must form a convex (left) turn for CCW polygon
                if (cross2d(A, B, C) < 0.f) continue;

                // No other vertex must lie strictly inside triangle ABC
                if (anyVertexInsideTri(poly, idx, prev, i, next)) continue;

                // Clip the ear
                result.push_back(static_cast<std::uint32_t>(idx[prev]));
                result.push_back(static_cast<std::uint32_t>(idx[i   ]));
                result.push_back(static_cast<std::uint32_t>(idx[next]));
                idx.erase(idx.begin() + i);
                clipped = true;
                break;
            }

            // No ear found on this pass — degenerate polygon, bail out
            if (!clipped) break;
        }

        // Last triangle
        if (idx.size() == 3) {
            result.push_back(static_cast<std::uint32_t>(idx[0]));
            result.push_back(static_cast<std::uint32_t>(idx[1]));
            result.push_back(static_cast<std::uint32_t>(idx[2]));
        }

        return result;
    }

    /// Signed area of the polygon (positive = CCW).
    [[nodiscard]]
    static float SignedArea(const std::vector<Vec2>& poly) noexcept
    {
        std::vector<int> idx(poly.size());
        std::iota(idx.begin(), idx.end(), 0);
        return signedArea(poly, idx);
    }

    [[nodiscard]]
    static bool IsCCW(const std::vector<Vec2>& poly) noexcept
    {
        return SignedArea(poly) > 0.f;
    }

private:
    // 2D cross product of vectors (A→B) × (A→C)
    static float cross2d(const Vec2& A, const Vec2& B, const Vec2& C) noexcept {
        return (B.x - A.x) * (C.y - A.y)
             - (B.y - A.y) * (C.x - A.x);
    }

    // Shoelace formula on an indexed polygon
    static float signedArea(const std::vector<Vec2>& poly,
                             const std::vector<int>&  idx) noexcept
    {
        float area = 0.f;
        const int n = static_cast<int>(idx.size());
        for (int i = 0; i < n; ++i) {
            const Vec2& a = poly[idx[i]];
            const Vec2& b = poly[idx[(i + 1) % n]];
            area += a.x * b.y - b.x * a.y;
        }
        return area * 0.5f;
    }

    /// Point-in-triangle test (strict: returns false for points on the edge).
    static bool pointInTri(const Vec2& P,
                            const Vec2& A, const Vec2& B, const Vec2& C) noexcept
    {
        const float d1 = cross2d(A, B, P);
        const float d2 = cross2d(B, C, P);
        const float d3 = cross2d(C, A, P);
        const bool hasNeg = (d1 < 0.f) || (d2 < 0.f) || (d3 < 0.f);
        const bool hasPos = (d1 > 0.f) || (d2 > 0.f) || (d3 > 0.f);
        return !(hasNeg && hasPos);
    }

    /// Returns true if any vertex in idx (excluding prev/cur/next) lies
    /// strictly inside triangle poly[idx[prev]], poly[idx[cur]], poly[idx[next]].
    static bool anyVertexInsideTri(
        const std::vector<Vec2>& poly,
        const std::vector<int>&  idx,
        int prev, int cur, int next) noexcept
    {
        const Vec2& A = poly[idx[prev]];
        const Vec2& B = poly[idx[cur ]];
        const Vec2& C = poly[idx[next]];
        const int m = static_cast<int>(idx.size());
        for (int i = 0; i < m; ++i) {
            if (i == prev || i == cur || i == next) continue;
            if (pointInTri(poly[idx[i]], A, B, C)) return true;
        }
        return false;
    }
};

} // namespace pelpaint::ir
