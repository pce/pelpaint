#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <span>
#include <memory>
#include "../core/Types.hpp"

namespace pelpaint::slicer {

/// Depth range [lo, hi] in normalised [0, 1] space.
struct DepthRange {
    float lo = 0.f;
    float hi = 1.f;
};

/// How empty regions are filled when slices separate.
enum class FillMode {
    None,               ///< fully transparent (raw mask only)
    Clone,              ///< flood-fill from nearest opaque edge pixel
    PatchAverage,       ///< N×N neighbourhood average (configurable)
    PatchGrid,          ///< tiled average in 8×8 grid cells
    Inpaint,            ///< future: diffusion/ML inpainting
};

/// Per-slice fill configuration.
struct FillStyle {
    FillMode  mode        = FillMode::Clone;
    int       patchRadius = 4;    ///< used by PatchAverage / PatchGrid
    float     featherRadius = 2.f; ///< alpha fade at edge (pixels)
};

/// Layer blend mode applied during composite.
enum class BlendMode {
    Normal, Screen, Overlay, SoftLight,
    Multiply, Additive, Glow,
};

/// Parallax + camera motion descriptor.
struct MotionDesc {
    float parallaxFactor = 1.f;  ///< 0 = static; 1 = full camera delta
    float driftAmplitude = 0.f;  ///< sinusoidal world-space drift (px)
    float driftFrequency = 1.f;  ///< Hz
    float noiseStrength  = 0.f;  ///< Perlin-noise wander
};

/// Procedural animation applied each frame.
enum class AnimPreset {
    None, WindSway, Breathe, WaveDistort,
    Turbulence, CameraShake, ElasticLag,
};

/// Virtual light reaction.
struct LightReaction {
    bool  enabled        = false;
    float rimStrength    = 0.f;
    float shadowStrength = 0.f;
    float ambientBias    = 0.f;
};

/// One atomic scene layer produced by any SliceGenerator.
struct Slice {
    // --- identity ---
    int         id       = 0;
    std::string label;           ///< e.g. "foreground", "hair", "mountains"
    int         parentId = -1;   ///< -1 = root;  > 0 = child of another slice

    // --- pixel data ---
    std::vector<uint8_t> pixels; ///< RGBA8, w × h bytes
    int                  width  = 0;
    int                  height = 0;

    // --- depth ---
    DepthRange depth;
    float      parallaxFactor = 1.f;

    // --- spatial ---
    Point2f    offset       = {0.f, 0.f}; ///< current world-space translation
    float      scale        = 1.f;         ///< uniform scale (pop-out effect)
    float      rotation     = 0.f;         ///< radians

    // --- contour ---
    std::vector<Point2f> contour; ///< simplified polygon (RDP-reduced)

    // --- appearance ---
    FillStyle   fill;
    BlendMode   blendMode = BlendMode::Normal;
    float       opacity   = 1.f;

    // --- motion ---
    MotionDesc  motion;
    AnimPreset  animPreset = AnimPreset::None;

    // --- lighting ---
    LightReaction light;
};

struct GeneratorOptions {
    int   numSlices     = 5;
    float depthSmooth   = 1.f;
    bool  invertDepth   = false;
    bool  featherEdges  = true;
    FillMode fillMode   = FillMode::Clone;
};

struct SceneContext {
    Point2f cameraDelta  = {0.f, 0.f};
    float   time         = 0.f;
    Point2f shakeVector  = {0.f, 0.f};
};

class Modifier {
public:
    virtual ~Modifier() = default;
    virtual void Apply(Slice& slice, float dt, const SceneContext& ctx) = 0;
    bool enabled = true;
};

class ModifierStack {
    std::vector<std::unique_ptr<Modifier>> modifiers_;
public:
    void Add(std::unique_ptr<Modifier> m) { modifiers_.push_back(std::move(m)); }
    void Apply(Slice& s, float dt, const SceneContext& ctx) {
        for (auto& m : modifiers_)
            if (m->enabled) m->Apply(s, dt, ctx);
    }
};

class SliceGenerator {
public:
    virtual ~SliceGenerator() = default;
    virtual std::vector<Slice> Generate(
        std::span<const uint8_t> rgba,   // source image  W×H×4
        std::span<const uint8_t> depth,  // depth map     W×H×1
        int w, int h,
        const GeneratorOptions& opts) = 0;
};

class DepthThresholdGenerator : public SliceGenerator {
public:
    std::vector<Slice> Generate(
        std::span<const uint8_t> rgba,
        std::span<const uint8_t> depth,
        int w, int h,
        const GeneratorOptions& opts) override;
};

class FillHolesModifier : public Modifier {
public:
    void Apply(Slice& slice, float dt, const SceneContext& ctx) override;
};

class FeatherEdgeModifier : public Modifier {
public:
    void Apply(Slice& slice, float dt, const SceneContext& ctx) override;
};

class ParallaxMotionModifier : public Modifier {
public:
    void Apply(Slice& slice, float dt, const SceneContext& ctx) override;
};

class ProceduralAnimModifier : public Modifier {
public:
    void Apply(Slice& slice, float dt, const SceneContext& ctx) override;
};

} // namespace pelpaint::slicer
