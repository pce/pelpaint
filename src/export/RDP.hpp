#pragma once

/** @file
 * @brief Ramer-Douglas-Peucker polyline / polygon simplification.
 */

#include "RegionGraph.hpp"   // Vec2
#include <vector>
#include <cmath>
#include <algorithm>

namespace pelpaint::ir {

class RDP {
public:
    /**
     * @brief Simplify an open polyline.  The first and last points are always kept.
     *
     * @par Typical epsilon values for pixel art
     * | Value | Effect                                                       |
     * |-------|--------------------------------------------------------------|
     * | 0.0   | lossless (removes only exactly collinear points)             |
     * | 0.5   | sub-pixel: preserves sharp corners, removes stair-step noise |
     * | 1.0   | 1 px tolerance: good for sharp pixel-art edges               |
     * | 2.0+  | noticeable rounding; use for photo/painterly content         |
     */
    [[nodiscard]]
    static std::vector<Vec2> Simplify(
        const std::vector<Vec2>& pts,
        float                    epsilon = 1.0f)
    {
        if (pts.size() < 3) return pts;
        std::vector<Vec2> result;
        result.reserve(pts.size());
        rdp(pts, 0, static_cast<int>(pts.size()) - 1, epsilon * epsilon, result);
        result.push_back(pts.back());
        return result;
    }

    /// Simplify a closed polygon.
    /// The input is treated as a loop: pts.back() connects back to pts.front().
    [[nodiscard]]
    static std::vector<Vec2> SimplifyPolygon(
        const std::vector<Vec2>& pts,
        float                    epsilon = 1.0f)
    {
        if (pts.size() < 4) return pts;

        // Find the two vertices that are farthest from each other — use them
        // as the guaranteed split points so neither is lost.
        int a = 0, b = 1;
        float maxDist = 0.f;
        const int n = static_cast<int>(pts.size());
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                const float d = dist2(pts[i], pts[j]);
                if (d > maxDist) { maxDist = d; a = i; b = j; }
            }
        }

        // Build two open chains: a→b and b→wrap→a
        std::vector<Vec2> chain1(pts.begin() + a, pts.begin() + b + 1);
        std::vector<Vec2> chain2;
        chain2.reserve(n - b + a + 1);
        for (int i = b; i < n; ++i)  chain2.push_back(pts[i]);
        for (int i = 0; i <= a; ++i) chain2.push_back(pts[i]);

        auto s1 = Simplify(chain1, epsilon);
        auto s2 = Simplify(chain2, epsilon);

        // Merge (shared endpoints are not duplicated)
        std::vector<Vec2> result;
        result.reserve(s1.size() + s2.size());
        result.insert(result.end(), s1.begin(), s1.end());
        // s2.front() == s1.back() and s2.back() == s1.front() — skip both
        for (int i = 1; i < static_cast<int>(s2.size()) - 1; ++i)
            result.push_back(s2[i]);

        return result;
    }

    /// Remove exactly collinear points (epsilon = 0 case, integer-grid safe).
    [[nodiscard]]
    static std::vector<Vec2> RemoveCollinear(const std::vector<Vec2>& pts)
    {
        return SimplifyPolygon(pts, 0.0f);
    }

private:
    static float dist2(const Vec2& a, const Vec2& b) noexcept {
        const float dx = a.x - b.x, dy = a.y - b.y;
        return dx*dx + dy*dy;
    }

    /// Perpendicular distance² from point p to the segment (a, b).
    static float perp2(const Vec2& p, const Vec2& a, const Vec2& b) noexcept {
        const Vec2 ab{ b.x-a.x, b.y-a.y };
        const float len2 = ab.x*ab.x + ab.y*ab.y;
        if (len2 < 1e-10f) return dist2(p, a);

        // Project p onto ab, clamp to [0,1]
        const float t = std::clamp(
            ((p.x-a.x)*ab.x + (p.y-a.y)*ab.y) / len2,
            0.f, 1.f);

        const Vec2 proj{ a.x + t*ab.x, a.y + t*ab.y };
        return dist2(p, proj);
    }

    /// Recursive DPR worker.  Appends kept points to `out` (excluding the
    /// last point of each sub-range so callers can chain without duplication).
    static void rdp(
        const std::vector<Vec2>& pts,
        int                       lo,
        int                       hi,
        float                     eps2,
        std::vector<Vec2>&        out)
    {
        // Always keep lo
        out.push_back(pts[lo]);

        if (hi - lo < 2) return;

        // Find the point with the maximum perpendicular distance to (lo, hi)
        float maxD2 = 0.f;
        int   split = lo;
        for (int i = lo + 1; i < hi; ++i) {
            const float d2 = perp2(pts[i], pts[lo], pts[hi]);
            if (d2 > maxD2) { maxD2 = d2; split = i; }
        }

        if (maxD2 > eps2) {
            // Significant deviation — recurse into both halves
            rdp(pts, lo,    split, eps2, out);
            rdp(pts, split, hi,    eps2, out);
        }
        // else: all points between lo and hi are within epsilon — skip them
    }
};

} // namespace pelpaint::ir
