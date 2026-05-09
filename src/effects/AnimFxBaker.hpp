#pragma once

// ---------------------------------------------------------------------------
// AnimFxBaker — multi-frame animation bakers for noise and particle effects,
// plus a frame-smoothing utility.
//
//  BakeNoiseFx       — N frames of time-animated value noise, optionally
//                      composited over the current canvas layers.
//
//  BakeParticleFx    — N frames stepping a ParticleSystem simulation,
//                      optionally composited over the canvas.
//
//  InsertSmoothFrames — post-bake pass: inserts linearly-interpolated
//                       (lerp) frames between every consecutive pair in
//                       the timeline so playback crossfades smoothly instead
//                       of cutting directly between frames.  Eliminates the
//                       strobe-light effect on sparse timelines.
// ---------------------------------------------------------------------------

#include <expected>
#include <vector>

#include "../core/Error.hpp"
#include "../core/AnimationTimeline.hpp"
#include "../core/Canvas.hpp"
#include "../tools/ParticleSystem.hpp"

namespace pelpaint::effects {

// ===========================================================================
// NoiseFxConfig — controls for the animated-noise baker
// ===========================================================================

struct NoiseFxConfig {
    int                frameCount   = 16;    ///< Number of frames to append
    float              noiseScale   = 0.05f; ///< Perlin spatial frequency
    float              timeSpeed    = 0.25f; ///< t-axis advance per frame (0=static)
    std::vector<Pixel> gradient;             ///< Colour ramp; must have ≥ 2 entries
    float              frameDelay   = 0.f;   ///< Per-frame delay; 0 = use timeline FPS
    bool               composite    = true;  ///< Overlay noise on top of canvas composite
    float              noiseOpacity = 1.f;   ///< Noise layer opacity [0..1]
    bool               smooth       = true;  ///< Insert lerp frames between baked frames
    int                smoothSteps  = 1;     ///< Interpolated frames per transition (1–8)
};

/// Bake animated noise frames into the timeline.
/// Returns the list of newly added frame indices on success.
[[nodiscard]]
std::expected<std::vector<int>, pelpaint::Error>
BakeNoiseFx(core::AnimationTimeline&  timeline,
            const Canvas&              canvas,
            const NoiseFxConfig&       cfg);

// ===========================================================================
// ParticleFxConfig — controls for the particle-simulation baker
// ===========================================================================

struct ParticleFxConfig {
    tools::ParticleConfig particle;              ///< Standard particle parameters
    int                   frameCount  = 24;      ///< Total frames to bake
    float                 simFps      = 24.f;    ///< Simulation tick rate (frames/sec)
    float                 frameDelay  = 0.f;     ///< Per-frame delay; 0 = use timeline FPS
    bool                  composite   = true;    ///< Draw particles over canvas composite
    bool                  smooth      = true;
    int                   smoothSteps = 1;
    Point2f               emitterPos  = {-1.f, -1.f}; ///< (-1,-1) = canvas centre
};

/// Bake a full particle-simulation run as individual animation frames.
/// Returns the list of newly added frame indices on success.
[[nodiscard]]
std::expected<std::vector<int>, pelpaint::Error>
BakeParticleFx(core::AnimationTimeline& timeline,
               const Canvas&             canvas,
               const ParticleFxConfig&   cfg);

// ===========================================================================
// SmoothConfig — post-bake frame interpolation pass
// ===========================================================================

struct SmoothConfig {
    int  stepsPerTransition = 1;    ///< Lerp frames inserted between each pair (1–8)
    bool wrap               = false; ///< Also smooth last → first transition (loop)
};

/// Insert linearly-interpolated frames between every consecutive pair of
/// existing timeline frames.  Processes pairs right-to-left so insertion
/// indices stay stable.
/// Returns the total number of frames added on success.
[[nodiscard]]
std::expected<int, pelpaint::Error>
InsertSmoothFrames(core::AnimationTimeline& timeline, const SmoothConfig& cfg);

} // namespace pelpaint::effects
