// =============================================================================
// Triangulate.hpp
//
// Self-contained implementation of the Triangula-style genetic-algorithm
// image triangulation filter.  Produces a flat-shaded Delaunay mesh where
// each triangle carries the average colour of the source pixels it covers.
//
// Public surface (called from Filters.cpp):
//   std::vector<pelpaint::Pixel>
//   TriangulateImage(src, W, H, opts, progressFn = nullptr)
//
// TriangulateOptions is defined in Filters.hpp (included below).
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <random>
#include <span>
#include <vector>

#include "Filters.hpp"   // TriangulateOptions, pelpaint::Pixel, pelpaint::filters

namespace pelpaint::filters {

// =============================================================================
// Internal implementation — hidden in detail_tri namespace
// =============================================================================
namespace detail_tri {

// ---------------------------------------------------------------------------
// Primitive types
// ---------------------------------------------------------------------------
struct Pt   { float x, y; };
struct Tri  { int   a, b, c; };
struct Edge { int   a, b;    };

/// Record of one point mutation (index + new position).
struct MutRec { int idx;  Pt newPos; };

/// One member of the GA population.
struct Member {
    std::vector<Pt>     points;
    double              ssd           = 1e18;
    std::vector<MutRec> lastMutations;  // filled by mutateMember
};

// ---------------------------------------------------------------------------
// Circumcircle test  (double precision; assumes CCW triangle orientation)
//
// Returns true when point p lies strictly inside the circumcircle of
// triangle (a, b, c).  The sign of the determinant flips for CW triangles,
// so all triangles in the mesh must keep a consistent CCW winding.
// ---------------------------------------------------------------------------
[[nodiscard]] static bool inCircumcircle(
    const Pt& p, const Pt& a, const Pt& b, const Pt& c) noexcept
{
    const double ax = a.x - p.x,  ay = a.y - p.y;
    const double bx = b.x - p.x,  by = b.y - p.y;
    const double cx = c.x - p.x,  cy = c.y - p.y;
    const double det =
        ax * (by * (cx*cx + cy*cy) - cy * (bx*bx + by*by))
      - ay * (bx * (cx*cx + cy*cy) - cx * (bx*bx + by*by))
      + (ax*ax + ay*ay) * (bx*cy - by*cx);
    return det > 0.0;
}

// ---------------------------------------------------------------------------
// Bowyer-Watson incremental Delaunay triangulation
//
// Super-triangle vertices are ordered CCW so that every triangle produced
// by the algorithm inherits a consistent CCW winding, keeping the
// inCircumcircle test sign-correct throughout.
// ---------------------------------------------------------------------------
[[nodiscard]] static std::vector<Tri>
delaunay(const std::vector<Pt>& pts, int W, int H)
{
    const int n = static_cast<int>(pts.size());
    if (n < 3) return {};

    // Working copy — will grow by 3 super-triangle vertices
    std::vector<Pt> all(pts);

    // Super-triangle: CCW winding — top, bottom-right, bottom-left
    //   Cross product (v1-v0) x (v2-v0) must be positive (z > 0).
    //   v0=(cx, cy-2m), v1=(cx+2m, cy+m), v2=(cx-2m, cy+m)
    //   (v1-v0) = (2m, 3m),  (v2-v0) = (-2m, 3m)
    //   z = 2m*3m - 3m*(-2m) = 6m² + 6m² = 12m² > 0  ✓
    const float margin = static_cast<float>(std::max(W, H)) * 10.0f;
    const float cx = W * 0.5f, cy = H * 0.5f;
    const int si0 = n, si1 = n + 1, si2 = n + 2;
    all.push_back({ cx,                cy - margin * 2.0f }); // top
    all.push_back({ cx + margin * 2.0f, cy + margin        }); // bottom-right
    all.push_back({ cx - margin * 2.0f, cy + margin        }); // bottom-left

    std::vector<Tri>  tris;
    tris.reserve(static_cast<size_t>(n) * 3);
    tris.push_back({ si0, si1, si2 });

    // Reuse these per-point to avoid repeated allocations
    std::vector<int>  bad;
    std::vector<Edge> boundary;

    for (int pi = 0; pi < n; ++pi) {
        const Pt& p = all[pi];

        // ---- find all triangles whose circumcircle contains p ------------
        bad.clear();
        for (int ti = 0; ti < static_cast<int>(tris.size()); ++ti) {
            const Tri& t = tris[ti];
            if (inCircumcircle(p, all[t.a], all[t.b], all[t.c]))
                bad.push_back(ti);
        }

        // ---- boundary polygon of the hole --------------------------------
        // An edge belongs to the boundary iff it is *not* shared by
        // two distinct bad triangles (it is shared by at most one bad tri
        // plus one good tri — or it is purely on the outer boundary).
        boundary.clear();
        for (int bi : bad) {
            const Tri&  t     = tris[bi];
            const Edge  ev[3] = {{ t.a, t.b }, { t.b, t.c }, { t.c, t.a }};
            for (const Edge& e : ev) {
                bool shared = false;
                for (int bi2 : bad) {
                    if (bi2 == bi) continue;
                    const Tri&  t2     = tris[bi2];
                    const Edge  ev2[3] = {{ t2.a, t2.b }, { t2.b, t2.c }, { t2.c, t2.a }};
                    for (const Edge& e2 : ev2) {
                        if ((e.a == e2.a && e.b == e2.b) ||
                            (e.a == e2.b && e.b == e2.a)) {
                            shared = true;
                            break;
                        }
                    }
                    if (shared) break;
                }
                if (!shared) boundary.push_back(e);
            }
        }

        // ---- remove bad triangles (reverse order preserves indices) ------
        std::sort(bad.rbegin(), bad.rend());
        for (int bi : bad)
            tris.erase(tris.begin() + bi);

        // ---- fill hole with new CCW triangles ----------------------------
        // Boundary edges are oriented CCW around the hole; connecting
        // {e.a, e.b, newPoint} maintains CCW winding for all new tris.
        for (const Edge& e : boundary)
            tris.push_back({ e.a, e.b, pi });
    }

    // ---- remove triangles that touch the super-triangle vertices ---------
    tris.erase(std::remove_if(tris.begin(), tris.end(),
        [si0, si1, si2](const Tri& t) {
            return t.a == si0 || t.a == si1 || t.a == si2
                || t.b == si0 || t.b == si1 || t.b == si2
                || t.c == si0 || t.c == si1 || t.c == si2;
        }), tris.end());

    return tris;
}

// ---------------------------------------------------------------------------
// Scanline helpers
// ---------------------------------------------------------------------------
[[nodiscard]] static inline uint8_t clamp8t(int v) noexcept {
    return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

struct AvgColor { double r, g, b, a; int count; };

/// Compute the average RGBA of all non-transparent source pixels inside
/// triangle (p0, p1, p2) using a horizontal scanline rasterizer.
[[nodiscard]] static AvgColor computeTriAvgColor(
    std::span<const pelpaint::Pixel> src,
    int W, int H,
    const Pt& p0, const Pt& p1, const Pt& p2,
    uint8_t alphaThr)
{
    const int yMin = std::max(0,     (int)std::floor(std::min({ p0.y, p1.y, p2.y })));
    const int yMax = std::min(H - 1, (int)std::floor(std::max({ p0.y, p1.y, p2.y })));

    const Pt* ev[3][2] = {{ &p0, &p1 }, { &p1, &p2 }, { &p2, &p0 }};
    double sumR = 0, sumG = 0, sumB = 0, sumA = 0;
    int count = 0;

    for (int y = yMin; y <= yMax; ++y) {
        float xMin = static_cast<float>(W), xMax = -1.0f;
        for (int ei = 0; ei < 3; ++ei) {
            const Pt& A = *ev[ei][0];
            const Pt& B = *ev[ei][1];
            // fa / fb: signed distance of each vertex from the scanline y+0.5
            const float fa = A.y - (y + 0.5f);
            const float fb = B.y - (y + 0.5f);
            if ((fa < 0.0f) != (fb < 0.0f)) {    // edge crosses this scanline
                const float t = fa / (fa - fb);
                const float x = A.x + t * (B.x - A.x);
                xMin = std::min(xMin, x);
                xMax = std::max(xMax, x);
            }
        }
        if (xMin > xMax) continue;
        const int x0 = std::max(0,     (int)std::ceil(xMin));
        const int x1 = std::min(W - 1, (int)std::floor(xMax));
        for (int x = x0; x <= x1; ++x) {
            const auto& px = src[y * W + x];
            if (px.a >= alphaThr) {
                sumR += px.r;  sumG += px.g;
                sumB += px.b;  sumA += px.a;
                ++count;
            }
        }
    }
    return { sumR, sumG, sumB, sumA, count };
}

/// Paint triangle (p0, p1, p2) into dst with flat colour (r,g,b,a).
/// Only pixels whose *source* alpha >= alphaThr are overwritten; transparent
/// source pixels are left unchanged.
static void rasterizeTriangle(
    std::vector<pelpaint::Pixel>&    dst,
    std::span<const pelpaint::Pixel> src,
    int W, int H,
    const Pt& p0, const Pt& p1, const Pt& p2,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a,
    uint8_t alphaThr)
{
    const int yMin = std::max(0,     (int)std::floor(std::min({ p0.y, p1.y, p2.y })));
    const int yMax = std::min(H - 1, (int)std::floor(std::max({ p0.y, p1.y, p2.y })));

    const Pt* ev[3][2] = {{ &p0, &p1 }, { &p1, &p2 }, { &p2, &p0 }};

    for (int y = yMin; y <= yMax; ++y) {
        float xMin = static_cast<float>(W), xMax = -1.0f;
        for (int ei = 0; ei < 3; ++ei) {
            const Pt& A = *ev[ei][0];
            const Pt& B = *ev[ei][1];
            const float fa = A.y - (y + 0.5f);
            const float fb = B.y - (y + 0.5f);
            if ((fa < 0.0f) != (fb < 0.0f)) {
                const float t = fa / (fa - fb);
                const float x = A.x + t * (B.x - A.x);
                xMin = std::min(xMin, x);
                xMax = std::max(xMax, x);
            }
        }
        if (xMin > xMax) continue;
        const int x0 = std::max(0,     (int)std::ceil(xMin));
        const int x1 = std::min(W - 1, (int)std::floor(xMax));
        for (int x = x0; x <= x1; ++x) {
            if (src[y * W + x].a >= alphaThr)
                dst[y * W + x] = pelpaint::Pixel{ r, g, b, a };
        }
    }
}

// ---------------------------------------------------------------------------
// Render a set of control points as a fully triangulated image
// ---------------------------------------------------------------------------
[[nodiscard]] static std::vector<pelpaint::Pixel> renderTriangulation(
    std::span<const pelpaint::Pixel> src,
    int W, int H,
    const std::vector<Pt>& pts,
    uint8_t alphaThr)
{
    // Start from the source (preserves transparent pixels by default)
    std::vector<pelpaint::Pixel> dst(src.begin(), src.end());

    const auto tris = delaunay(pts, W, H);
    for (const Tri& tri : tris) {
        const Pt& p0 = pts[tri.a];
        const Pt& p1 = pts[tri.b];
        const Pt& p2 = pts[tri.c];

        // Skip degenerate triangles (area < 0.5 px²)
        const float area =
            std::abs((p1.x - p0.x) * (p2.y - p0.y)
                   - (p2.x - p0.x) * (p1.y - p0.y)) * 0.5f;
        if (area < 0.5f) continue;

        const AvgColor avg = computeTriAvgColor(src, W, H, p0, p1, p2, alphaThr);
        if (avg.count == 0) continue;   // fully transparent — nothing to paint

        const uint8_t pr = clamp8t(static_cast<int>(avg.r / avg.count));
        const uint8_t pg = clamp8t(static_cast<int>(avg.g / avg.count));
        const uint8_t pb = clamp8t(static_cast<int>(avg.b / avg.count));
        const uint8_t pa = clamp8t(static_cast<int>(avg.a / avg.count));

        rasterizeTriangle(dst, src, W, H, p0, p1, p2, pr, pg, pb, pa, alphaThr);
    }
    return dst;
}

// ---------------------------------------------------------------------------
// Fitness: sum of squared RGB differences over all non-transparent pixels
// ---------------------------------------------------------------------------
[[nodiscard]] static double computeSSD(
    const std::vector<pelpaint::Pixel>& rendered,
    std::span<const pelpaint::Pixel>    original,
    int N,
    uint8_t alphaThr) noexcept
{
    double ssd = 0.0;
    for (int i = 0; i < N; ++i) {
        if (original[i].a < alphaThr) continue;
        const double dr = static_cast<double>(rendered[i].r) - original[i].r;
        const double dg = static_cast<double>(rendered[i].g) - original[i].g;
        const double db = static_cast<double>(rendered[i].b) - original[i].b;
        ssd += dr*dr + dg*dg + db*db;
    }
    return ssd;
}

/// Render + score a member in-place.
static void evaluateMember(
    Member&                          m,
    std::span<const pelpaint::Pixel> src,
    int W, int H,
    uint8_t alphaThr)
{
    const auto rendered = renderTriangulation(src, W, H, m.points, alphaThr);
    m.ssd = computeSSD(rendered, src, W * H, alphaThr);
}

// ---------------------------------------------------------------------------
// Sobel edge-magnitude map (luminance-based, 3×3 kernel)
// ---------------------------------------------------------------------------
[[nodiscard]] static std::vector<float> sobelEdgeMap(
    std::span<const pelpaint::Pixel> src, int W, int H)
{
    std::vector<float> mag(static_cast<size_t>(W * H), 0.0f);

    auto lum = [&](int x, int y) noexcept -> float {
        const auto& p = src[y * W + x];
        return 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
    };

    for (int y = 1; y < H - 1; ++y) {
        for (int x = 1; x < W - 1; ++x) {
            const float tl = lum(x-1,y-1), tc = lum(x,y-1), tr = lum(x+1,y-1);
            const float ml = lum(x-1,y  ),                   mr = lum(x+1,y  );
            const float bl = lum(x-1,y+1), bc = lum(x,y+1), br = lum(x+1,y+1);
            const float gx = -tl - 2.0f*ml - bl + tr + 2.0f*mr + br;
            const float gy = -tl - 2.0f*tc - tr + bl + 2.0f*bc + br;
            mag[y * W + x] = std::sqrt(gx*gx + gy*gy);
        }
    }
    return mag;
}

// ---------------------------------------------------------------------------
// Build the initial point set for one population member
// ---------------------------------------------------------------------------
[[nodiscard]] static std::vector<Pt> buildInitialPoints(
    std::span<const pelpaint::Pixel> src,
    int W, int H,
    int numPoints,
    bool edgeBias,
    uint8_t alphaThr,
    std::mt19937& rng)
{
    std::vector<Pt> pts;
    pts.reserve(static_cast<size_t>(numPoints) + 4);

    // Always anchor the four image corners so the triangulation covers the
    // full canvas with no gaps at the edges.
    pts.push_back({ 0.0f,           0.0f           });
    pts.push_back({ (float)(W - 1), 0.0f           });
    pts.push_back({ 0.0f,           (float)(H - 1) });
    pts.push_back({ (float)(W - 1), (float)(H - 1) });

    // Collect non-transparent candidate pixel indices
    std::vector<int> cands;
    cands.reserve(static_cast<size_t>(W * H));
    for (int i = 0; i < W * H; ++i)
        if (src[i].a >= alphaThr)
            cands.push_back(i);

    if (cands.empty()) {                // fall back to all pixels
        for (int i = 0; i < W * H; ++i)
            cands.push_back(i);
    }

    const int needed = std::max(0, numPoints - 4);
    if (needed == 0) return pts;

    if (edgeBias) {
        // Build a cumulative-weight array from the Sobel edge magnitude.
        // Every pixel gets weight = (edge_mag + 1) so no position has
        // probability zero.  The +1 also provides 50 % uniform floor once
        // the weights are normalised.
        const auto edgeMap = sobelEdgeMap(src, W, H);

        std::vector<float> cumul;
        cumul.reserve(cands.size());
        float total = 0.0f;
        for (int idx : cands) {
            total += edgeMap[idx] + 1.0f;
            cumul.push_back(total);
        }
        for (float& w : cumul) w /= total;  // normalise to [0, 1]

        std::uniform_real_distribution<float> unit(0.0f, 1.0f);

        // Half edge-biased, half uniform
        const int edgeCnt = needed / 2;
        const int unifCnt = needed - edgeCnt;

        for (int i = 0; i < edgeCnt; ++i) {
            const float rv = unit(rng);
            auto it = std::lower_bound(cumul.begin(), cumul.end(), rv);
            int ci = static_cast<int>(it - cumul.begin());
            ci = std::min(ci, static_cast<int>(cands.size()) - 1);
            const int idx = cands[ci];
            pts.push_back({ (float)(idx % W), (float)(idx / W) });
        }

        std::uniform_int_distribution<int> udist(0, (int)cands.size() - 1);
        for (int i = 0; i < unifCnt; ++i) {
            const int idx = cands[udist(rng)];
            pts.push_back({ (float)(idx % W), (float)(idx / W) });
        }
    } else {
        std::uniform_int_distribution<int> udist(0, (int)cands.size() - 1);
        for (int i = 0; i < needed; ++i) {
            const int idx = cands[udist(rng)];
            pts.push_back({ (float)(idx % W), (float)(idx / W) });
        }
    }

    return pts;
}

// ---------------------------------------------------------------------------
// Mutate a member: move `numMutations` random points by Gaussian noise
// ---------------------------------------------------------------------------
[[nodiscard]] static Member mutateMember(
    const Member& base,
    int W, int H,
    int numMutations,
    float variation,
    std::mt19937& rng)
{
    Member copy = base;
    copy.ssd    = 1e18;
    copy.lastMutations.clear();
    copy.lastMutations.reserve(static_cast<size_t>(numMutations));

    const float sigma = variation * static_cast<float>(std::max(W, H));
    std::normal_distribution<float>    gauss(0.0f, sigma);
    std::uniform_int_distribution<int> pick(0, (int)copy.points.size() - 1);

    for (int m = 0; m < numMutations; ++m) {
        const int   idx = pick(rng);
        const float nx  = std::clamp(copy.points[idx].x + gauss(rng),
                                     0.0f, (float)(W - 1));
        const float ny  = std::clamp(copy.points[idx].y + gauss(rng),
                                     0.0f, (float)(H - 1));
        copy.points[idx] = { nx, ny };
        copy.lastMutations.push_back({ idx, { nx, ny } });
    }
    return copy;
}

// ---------------------------------------------------------------------------
// Super-member: combine all beneficial mutations onto the base
// ---------------------------------------------------------------------------
[[nodiscard]] static Member createSuperMember(
    const Member&              base,
    const std::vector<Member>& beneficial)
{
    Member sup = base;
    sup.ssd    = 1e18;
    sup.lastMutations.clear();

    // Apply every beneficial mutation; last write on any given index wins.
    for (const Member& bc : beneficial)
        for (const MutRec& mut : bc.lastMutations)
            sup.points[mut.idx] = mut.newPos;

    return sup;
}

} // namespace detail_tri

// =============================================================================
// TriangulateImage — public inline entry point (called from Filters.cpp)
// =============================================================================

/// Run the full Triangula-style GA and return the rendered pixel buffer.
/// @param src        Source RGBA pixel buffer (row-major, width×height).
/// @param W          Image width in pixels.
/// @param H          Image height in pixels.
/// @param opts       Algorithm parameters (see TriangulateOptions in Filters.hpp).
/// @param progressFn Optional callback(currentGen, totalGens) → false to stop early.
[[nodiscard]] inline std::vector<pelpaint::Pixel>
TriangulateImage(
    std::span<const pelpaint::Pixel>               src,
    int                                            W,
    int                                            H,
    const TriangulateOptions&                      opts,
    std::function<bool(int,int)>                   progressFn = nullptr)
{
    using namespace detail_tri;

    // ---- guard -----------------------------------------------------------
    if (src.empty() || W <= 0 || H <= 0)
        return std::vector<pelpaint::Pixel>(src.begin(), src.end());

    // ---- auto-scale for large images -------------------------------------
    // Reduce point count and generation count proportionally so the filter
    // stays responsive at resolutions above 512 px.
    int numPoints   = opts.numPoints;
    int generations = opts.generations;
    const int maxDim = std::max(W, H);
    if (maxDim > 512) {
        const float scale = 512.0f / static_cast<float>(maxDim);
        numPoints   = std::max(10, static_cast<int>(numPoints   * scale));
        generations = std::max(5,  static_cast<int>(generations * scale));
    }

    // ---- RNG (non-deterministic seed for variety) ------------------------
    std::mt19937 rng{ std::random_device{}() };

    // ---- initialise population -------------------------------------------
    const int popSize       = opts.population;
    const int cutoff        = std::min(opts.cutoff, popSize);
    const int copiesPerBase = (popSize - cutoff) / std::max(1, cutoff);

    std::vector<Member> pop;
    pop.reserve(static_cast<size_t>(popSize));

    for (int i = 0; i < popSize; ++i) {
        Member m;
        m.points = buildInitialPoints(src, W, H, numPoints,
                                      opts.edgeBias, opts.alphaThreshold, rng);
        pop.push_back(std::move(m));
    }

    // ---- evaluate initial population ------------------------------------
    for (Member& m : pop)
        evaluateMember(m, src, W, H, opts.alphaThreshold);

    // Sort ascending (best = lowest SSD)
    auto bySSD = [](const Member& a, const Member& b) { return a.ssd < b.ssd; };
    std::sort(pop.begin(), pop.end(), bySSD);

    // ---- GA loop ---------------------------------------------------------
    for (int gen = 0; gen < generations; ++gen) {

        // Track which population slots belong to each base so we can later
        // identify beneficial mutations per base.
        std::vector<std::vector<int>> baseCopySlots(static_cast<size_t>(cutoff));
        for (int bi = 0; bi < cutoff; ++bi)
            baseCopySlots[bi].reserve(static_cast<size_t>(copiesPerBase));

        // Overwrite pop[cutoff .. popSize-1] with fresh mutated copies
        int slot = cutoff;
        for (int bi = 0; bi < cutoff && slot < popSize; ++bi) {
            for (int ci = 0; ci < copiesPerBase && slot < popSize; ++ci, ++slot) {
                pop[slot] = mutateMember(pop[bi], W, H,
                                         opts.mutations, opts.variation, rng);
                baseCopySlots[bi].push_back(slot);
            }
        }
        // Fill any leftover slot (integer-division remainder)
        while (slot < popSize) {
            pop[slot] = mutateMember(pop[0], W, H,
                                     opts.mutations, opts.variation, rng);
            ++slot;
        }

        // Evaluate all newly generated members
        for (int i = cutoff; i < popSize; ++i)
            evaluateMember(pop[i], src, W, H, opts.alphaThreshold);

        // Build super-members from the union of all beneficial mutations
        std::vector<Member> supers;
        supers.reserve(static_cast<size_t>(cutoff));

        for (int bi = 0; bi < cutoff; ++bi) {
            std::vector<Member> beneficial;
            for (int s : baseCopySlots[bi])
                if (pop[s].ssd < pop[bi].ssd)
                    beneficial.push_back(pop[s]);

            if (!beneficial.empty()) {
                Member sup = createSuperMember(pop[bi], beneficial);
                evaluateMember(sup, src, W, H, opts.alphaThreshold);
                supers.push_back(std::move(sup));
            }
        }

        // Merge current population with super-members, keep top popSize
        for (Member& sup : supers)
            pop.push_back(std::move(sup));

        std::sort(pop.begin(), pop.end(), bySSD);
        if (static_cast<int>(pop.size()) > popSize)
            pop.resize(static_cast<size_t>(popSize));

        // Optional progress callback — return false to abort early
        if (progressFn && !progressFn(gen + 1, generations)) break;
    }

    // ---- render best member ---------------------------------------------
    return renderTriangulation(src, W, H, pop[0].points, opts.alphaThreshold);
}

} // namespace pelpaint::filters
