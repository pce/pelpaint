#include "AnimFxBaker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../core/ImageSurface.hpp"   // WriteFlat, Flatten

namespace pelpaint::effects {

// ===========================================================================
// Internal helpers
// ===========================================================================

namespace {

// ---------------------------------------------------------------------------
// Pixel lerp

[[nodiscard]] constexpr Pixel LerpPx(const Pixel& a, const Pixel& b, float t) noexcept
{
    const auto lc = [](uint8_t x, uint8_t y, float f) -> uint8_t {
        return static_cast<uint8_t>(
            static_cast<float>(x)
            + f * (static_cast<float>(y) - static_cast<float>(x)));
    };
    return { lc(a.r,b.r,t), lc(a.g,b.g,t), lc(a.b,b.b,t), lc(a.a,b.a,t) };
}

// ---------------------------------------------------------------------------
// Value noise — 3-axis (x, y, time) with smoothstep trilinear interpolation.
// Entirely self-contained; no dependency on stb_perlin or PixelPerfectGenerator.

[[nodiscard]] float ValueNoise3(float x, float y, float z) noexcept
{
    // Integer corners of the unit cube around (x,y,z)
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int z0 = static_cast<int>(std::floor(z));

    // Fractional parts
    float fx = x - static_cast<float>(x0);
    float fy = y - static_cast<float>(y0);
    float fz = z - static_cast<float>(z0);

    // Smooth-step: 6t^5 - 15t^4 + 10t^3  (quintic, zero 1st and 2nd derivative at ends)
    const auto smooth = [](float f) { return f * f * f * (f * (f * 6.f - 15.f) + 10.f); };
    fx = smooth(fx); fy = smooth(fy); fz = smooth(fz);

    // Deterministic hash of three integers → [0, 1]
    const auto hash = [](int ix, int iy, int iz) -> float {
        unsigned h = (static_cast<unsigned>(ix) * 2654435761u)
                   ^ (static_cast<unsigned>(iy) * 2246822519u)
                   ^ (static_cast<unsigned>(iz) * 3266489917u);
        h ^= h >> 16; h *= 0x85ebca6bu;
        h ^= h >> 13; h *= 0xc2b2ae35u;
        h ^= h >> 16;
        return static_cast<float>(h) / static_cast<float>(std::numeric_limits<unsigned>::max());
    };

    // Sample 8 corners of the unit cube
    const float n000 = hash(x0,   y0,   z0  );
    const float n100 = hash(x0+1, y0,   z0  );
    const float n010 = hash(x0,   y0+1, z0  );
    const float n110 = hash(x0+1, y0+1, z0  );
    const float n001 = hash(x0,   y0,   z0+1);
    const float n101 = hash(x0+1, y0,   z0+1);
    const float n011 = hash(x0,   y0+1, z0+1);
    const float n111 = hash(x0+1, y0+1, z0+1);

    // Trilinear interpolation
    const float ix00 = n000 + fx * (n100 - n000);
    const float ix10 = n010 + fx * (n110 - n010);
    const float ix01 = n001 + fx * (n101 - n001);
    const float ix11 = n011 + fx * (n111 - n011);
    const float iy0  = ix00 + fy * (ix10 - ix00);
    const float iy1  = ix01 + fy * (ix11 - ix01);
    return iy0 + fz * (iy1 - iy0);
}

// ---------------------------------------------------------------------------
// Build a flat canvas composite into a Pixel vector.

[[nodiscard]] std::vector<Pixel> CompositeFlat(const Canvas& canvas)
{
    const_cast<Canvas&>(canvas).Composite();
    const core::ImageView view = canvas.CompositeSurface().Flatten();
    const auto* base = reinterpret_cast<const Pixel*>(view.data);
    return std::vector<Pixel>(
        base,
        base + static_cast<std::size_t>(canvas.Width()) * canvas.Height());
}

// ---------------------------------------------------------------------------
// Write a Pixel span into an AnimationFrame surface.

void WriteFramePixels(core::AnimationFrame&  frame,
                      std::span<const Pixel> pixels,
                      float                  delay)
{
    frame.surface.WriteFlat(std::span<const core::PixelRGBA8>{
        reinterpret_cast<const core::PixelRGBA8*>(pixels.data()),
        pixels.size()});
    if (delay > 0.f) frame.delay = delay;
}

// ---------------------------------------------------------------------------
// Porter-Duff "over": composite src (with opacity) on top of dst in-place.

void AlphaOver(std::vector<Pixel>& dst,
               std::span<const Pixel> src,
               float opacity = 1.f) noexcept
{
    const std::size_t n = std::min(dst.size(), src.size());
    for (std::size_t i = 0; i < n; ++i) {
        const Pixel& s = src[i];
        Pixel&       d = dst[i];
        const float sa = (static_cast<float>(s.a) / 255.f) * opacity;
        const float da =  static_cast<float>(d.a) / 255.f;
        const float oa = sa + da * (1.f - sa);
        if (oa <= 0.f) { d = {0,0,0,0}; continue; }
        const float inv = 1.f / oa;
        d.r = static_cast<uint8_t>((s.r * sa + d.r * da * (1.f - sa)) * inv);
        d.g = static_cast<uint8_t>((s.g * sa + d.g * da * (1.f - sa)) * inv);
        d.b = static_cast<uint8_t>((s.b * sa + d.b * da * (1.f - sa)) * inv);
        d.a = static_cast<uint8_t>(oa * 255.f);
    }
}

// ---------------------------------------------------------------------------
// Insert lerp frames between timeline.Frame(i) and timeline.Frame(i+1).
// Works on the already-flattened pixel data passed in so callers can cache it.

void InsertLerpsBetween(core::AnimationTimeline& timeline,
                        int                       idxA,
                        std::span<const Pixel>    pixA,
                        std::span<const Pixel>    pixB,
                        int                       steps,
                        float                     delay)
{
    const std::size_t nPx = pixA.size();
    for (int step = 1; step <= steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps + 1);
        std::vector<Pixel> lerped(nPx);
        for (std::size_t p = 0; p < nPx; ++p)
            lerped[p] = LerpPx(pixA[p], pixB[p], t);
        // Insert at idxA + step (step-1 frames already inserted to the right of A)
        timeline.InsertFrame(idxA + step);
        WriteFramePixels(timeline.Frame(idxA + step), lerped, delay);
    }
}

} // anonymous namespace


// ===========================================================================
// BakeNoiseFx
// ===========================================================================

std::expected<std::vector<int>, pelpaint::Error>
BakeNoiseFx(core::AnimationTimeline&  timeline,
            const Canvas&              canvas,
            const NoiseFxConfig&       cfg)
{
    // --- validate ----------------------------------------------------------
    if (cfg.frameCount < 1)
        return std::unexpected(pelpaint::Error{
            pelpaint::ErrorCode::InvalidDimensions, "frameCount must be ≥ 1"});
    if (cfg.gradient.size() < 2)
        return std::unexpected(pelpaint::Error{
            pelpaint::ErrorCode::EmptyPalette, "NoiseFxConfig gradient needs ≥ 2 colours"});

    const int W = canvas.Width();
    const int H = canvas.Height();
    const std::size_t nPx = static_cast<std::size_t>(W) * H;
    const int         gradN = static_cast<int>(cfg.gradient.size());

    // Optional background composite (done once; noise is composited on top)
    std::vector<Pixel> bgBase;
    if (cfg.composite) {
        bgBase = CompositeFlat(canvas);
    } else {
        bgBase.assign(nPx, Pixel{0, 0, 0, 255});
    }

    std::vector<int> addedIndices;
    addedIndices.reserve(static_cast<std::size_t>(cfg.frameCount));

    // Track the first baked-frame index so we can run smooth over [first, last]
    const int bakeStart = timeline.FrameCount();

    for (int fi = 0; fi < cfg.frameCount; ++fi) {
        const float t = static_cast<float>(fi) * cfg.timeSpeed;

        // Build noise layer
        std::vector<Pixel> noise(nPx);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float v = ValueNoise3(
                    static_cast<float>(x) * cfg.noiseScale,
                    static_cast<float>(y) * cfg.noiseScale,
                    t);
                v = std::clamp(v, 0.f, 1.f);
                // Map value to gradient colour
                const float gf = v * static_cast<float>(gradN - 1);
                const int   gi = std::min(static_cast<int>(gf), gradN - 2);
                const float gt = gf - static_cast<float>(gi);
                noise[y * W + x] = LerpPx(cfg.gradient[static_cast<std::size_t>(gi)],
                                           cfg.gradient[static_cast<std::size_t>(gi + 1)],
                                           gt);
            }
        }

        // Composite noise over background
        std::vector<Pixel> frame = bgBase;
        AlphaOver(frame, noise, cfg.noiseOpacity);

        // Append to timeline
        const int idx = timeline.AddFrame();
        auto& f = timeline.Frame(idx);
        char _noiseLabel[24];
        std::snprintf(_noiseLabel, sizeof(_noiseLabel), "noise_%02d", fi + 1);
        f.label = _noiseLabel;
        WriteFramePixels(f, frame, cfg.frameDelay);
        addedIndices.push_back(idx);
    }

    // --- optional smooth pass over the newly baked frames ------------------
    if (cfg.smooth && cfg.smoothSteps >= 1 && cfg.frameCount >= 2) {
        // Only smooth within the baked range [bakeStart, bakeStart+frameCount-1]
        // Work right-to-left so insertion keeps indices stable.
        const int bakeEnd = bakeStart + cfg.frameCount - 1;
        for (int i = bakeEnd - 1; i >= bakeStart; --i) {
            const core::ImageView vA = timeline.Frame(i).surface.Flatten();
            const auto* pA = reinterpret_cast<const Pixel*>(vA.data);
            const core::ImageView vB = timeline.Frame(i + 1).surface.Flatten();
            const auto* pB = reinterpret_cast<const Pixel*>(vB.data);
            InsertLerpsBetween(timeline, i,
                               {pA, nPx}, {pB, nPx},
                               cfg.smoothSteps,
                               cfg.frameDelay);
        }
    }

    return addedIndices;
}


// ===========================================================================
// BakeParticleFx
// ===========================================================================

std::expected<std::vector<int>, pelpaint::Error>
BakeParticleFx(core::AnimationTimeline& timeline,
               const Canvas&             canvas,
               const ParticleFxConfig&   cfg)
{
    if (cfg.frameCount < 1)
        return std::unexpected(pelpaint::Error{
            pelpaint::ErrorCode::InvalidDimensions, "frameCount must be ≥ 1"});
    if (cfg.simFps <= 0.f)
        return std::unexpected(pelpaint::Error{
            pelpaint::ErrorCode::InvalidDimensions, "simFps must be > 0"});

    const int W = canvas.Width();
    const int H = canvas.Height();
    const std::size_t nPx = static_cast<std::size_t>(W) * H;

    // Background composite (constant across all frames)
    std::vector<Pixel> bgBase;
    if (cfg.composite) {
        bgBase = CompositeFlat(canvas);
    } else {
        bgBase.assign(nPx, Pixel{0, 0, 0, 255});
    }

    // Emitter position — default to canvas centre
    Point2f emitter = cfg.emitterPos;
    if (emitter.x < 0.f) emitter.x = static_cast<float>(W) / 2.f;
    if (emitter.y < 0.f) emitter.y = static_cast<float>(H) / 2.f;

    // Initialise particle system
    tools::ParticleSystem ps;
    ps.Init(cfg.particle);

    const float dt = 1.f / cfg.simFps;

    std::vector<int> addedIndices;
    addedIndices.reserve(static_cast<std::size_t>(cfg.frameCount));

    const int bakeStart = timeline.FrameCount();

    for (int fi = 0; fi < cfg.frameCount; ++fi) {
        // Step the simulation by one tick
        ps.Update(dt, emitter);

        // Render particles into a transparent buffer then composite
        std::vector<Pixel> partBuf(nPx, Pixel{0, 0, 0, 0});
        ps.DrawToBuffer(partBuf, W, H);

        std::vector<Pixel> frame = bgBase;
        AlphaOver(frame, partBuf);

        const int idx = timeline.AddFrame();
        auto& f = timeline.Frame(idx);
        char _partLabel[24];
        std::snprintf(_partLabel, sizeof(_partLabel), "particle_%02d", fi + 1);
        f.label = _partLabel;
        WriteFramePixels(f, frame, cfg.frameDelay);
        addedIndices.push_back(idx);
    }

    // --- optional smooth pass ----------------------------------------------
    if (cfg.smooth && cfg.smoothSteps >= 1 && cfg.frameCount >= 2) {
        const int bakeEnd = bakeStart + cfg.frameCount - 1;
        for (int i = bakeEnd - 1; i >= bakeStart; --i) {
            const core::ImageView vA = timeline.Frame(i).surface.Flatten();
            const auto* pA = reinterpret_cast<const Pixel*>(vA.data);
            const core::ImageView vB = timeline.Frame(i + 1).surface.Flatten();
            const auto* pB = reinterpret_cast<const Pixel*>(vB.data);
            InsertLerpsBetween(timeline, i,
                               {pA, nPx}, {pB, nPx},
                               cfg.smoothSteps,
                               cfg.frameDelay);
        }
    }

    return addedIndices;
}


// ===========================================================================
// InsertSmoothFrames
// ===========================================================================

std::expected<int, pelpaint::Error>
InsertSmoothFrames(core::AnimationTimeline& timeline, const SmoothConfig& cfg)
{
    const int origCount = timeline.FrameCount();
    if (origCount < 2)
        return std::unexpected(pelpaint::Error{
            pelpaint::ErrorCode::InvalidDimensions,
            "Need at least 2 frames to smooth"});
    if (cfg.stepsPerTransition < 1)
        return 0;

    const std::size_t nPx =
        static_cast<std::size_t>(timeline.Frame(0).surface.Width()) *
        timeline.Frame(0).surface.Height();

    int added = 0;

    // Process consecutive pairs right-to-left to keep earlier indices stable.
    for (int i = origCount - 2; i >= 0; --i) {
        // Flatten both frames BEFORE any insertion for this pair.
        const core::ImageView vA = timeline.Frame(i).surface.Flatten();
        const auto* pA = reinterpret_cast<const Pixel*>(vA.data);
        const core::ImageView vB = timeline.Frame(i + 1).surface.Flatten();
        const auto* pB = reinterpret_cast<const Pixel*>(vB.data);
        const float delay = timeline.Frame(i).delay;

        InsertLerpsBetween(timeline, i,
                           {pA, nPx}, {pB, nPx},
                           cfg.stepsPerTransition,
                           delay);
        added += cfg.stepsPerTransition;
    }

    // Optionally smooth last → first (for looping animations)
    if (cfg.wrap) {
        const int last = timeline.FrameCount() - 1;
        const core::ImageView vLast = timeline.Frame(last).surface.Flatten();
        const auto* pLast = reinterpret_cast<const Pixel*>(vLast.data);
        const core::ImageView vFirst = timeline.Frame(0).surface.Flatten();
        const auto* pFirst = reinterpret_cast<const Pixel*>(vFirst.data);
        const float delay = timeline.Frame(last).delay;

        for (int step = 1; step <= cfg.stepsPerTransition; ++step) {
            const float t = static_cast<float>(step) /
                            static_cast<float>(cfg.stepsPerTransition + 1);
            std::vector<Pixel> lerped(nPx);
            for (std::size_t p = 0; p < nPx; ++p)
                lerped[p] = LerpPx(pLast[p], pFirst[p], t);
            const int newIdx = timeline.AddFrame();
            WriteFramePixels(timeline.Frame(newIdx), lerped, delay);
            ++added;
        }
    }

    return added;
}

} // namespace pelpaint::effects
