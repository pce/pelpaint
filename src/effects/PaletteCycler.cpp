#include "PaletteCycler.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "../operators/pp_cycle_palette.hpp"
#include "../filters/Filters.hpp"

namespace pelpaint::effects {

using pelpaint::core::AnimationFrame;
using pelpaint::core::AnimationTimeline;
using pelpaint::core::FrameLayerState;
using pelpaint::core::ImageSurface;
using pelpaint::core::PixelRGBA8;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// Porter-Duff "over": composite src on top of dst (in-place).
/// opacity modulates the source alpha before blending.
void AlphaOverAccum(std::vector<Pixel>& dst,
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

        if (oa <= 0.f) { d = {0, 0, 0, 0}; continue; }

        const float inv = 1.f / oa;
        d.r = static_cast<uint8_t>((s.r * sa + d.r * da * (1.f - sa)) * inv);
        d.g = static_cast<uint8_t>((s.g * sa + d.g * da * (1.f - sa)) * inv);
        d.b = static_cast<uint8_t>((s.b * sa + d.b * da * (1.f - sa)) * inv);
        d.a = static_cast<uint8_t>(oa * 255.f);
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// GeneratePaletteCycle
// ---------------------------------------------------------------------------
std::expected<PaletteCycleResult, pelpaint::Error>
GeneratePaletteCycle(AnimationTimeline&        timeline,
                     const Canvas&             canvas,
                     const PaletteCycleConfig& cfg)
{
    // ---- Validate inputs ---------------------------------------------------

    const std::vector<Layer>& layers = canvas.Layers();

    if (cfg.cyclingLayerIdx < 0 ||
        cfg.cyclingLayerIdx >= static_cast<int>(layers.size()))
        return std::unexpected(pelpaint::Error{
            pelpaint::ErrorCode::OutOfBounds, "cyclingLayerIdx out of range"});

    if (cfg.cycleColors.empty())
        return std::unexpected(pelpaint::Error::EmptyPalette());

    const int         N   = static_cast<int>(cfg.cycleColors.size());
    const int         W   = canvas.Width();
    const int         H   = canvas.Height();
    const std::size_t nPx = static_cast<std::size_t>(W) * H;

    const Layer& cycleLayer = layers[static_cast<std::size_t>(cfg.cyclingLayerIdx)];
    if (cycleLayer.pixelData.size() != nPx)
        return std::unexpected(pelpaint::Error{
            pelpaint::ErrorCode::InvalidDimensions,
            "Cycling layer pixel count does not match canvas dimensions"});

    // ---- Sort layer indices by zIndex (ascending = bottom to top) ----------

    std::vector<int> sortedIdx(layers.size());
    std::iota(sortedIdx.begin(), sortedIdx.end(), 0);
    std::stable_sort(sortedIdx.begin(), sortedIdx.end(),
        [&](int a, int b) {
            return layers[static_cast<std::size_t>(a)].zIndex <
                   layers[static_cast<std::size_t>(b)].zIndex;
        });

    // ---- Step 1: Build the static backdrop (all layers except cycling) -----

    std::vector<Pixel> staticBase;
    try { staticBase.assign(nPx, Pixel{0, 0, 0, 0}); }
    catch (...) { return std::unexpected(pelpaint::Error::AllocFailed()); }

    for (int li : sortedIdx) {
        if (li == cfg.cyclingLayerIdx) continue;
        const Layer& lay = layers[static_cast<std::size_t>(li)];
        if (!lay.visible || lay.pixelData.size() != nPx) continue;
        AlphaOverAccum(staticBase, lay.pixelData, lay.opacity);
    }

    // ---- Step 2: Prepare cycling layer (optionally quantized) --------------

    std::vector<Pixel> cycleBase;
    if (cfg.quantizeFirst) {
        auto res = filters::QuantiseToPalette(
            cycleLayer.pixelData,
            std::span<const Pixel>{cfg.cycleColors});
        if (!res) return std::unexpected(res.error());
        cycleBase = std::move(*res);
    } else {
        try { cycleBase = cycleLayer.pixelData; }
        catch (...) { return std::unexpected(pelpaint::Error::AllocFailed()); }
    }

    // ---- Step 3: Generate N frames (one per cycle step) --------------------

    PaletteCycleResult result;
    result.cycleLength = N;
    result.frameIndices.reserve(static_cast<std::size_t>(N));

    const pelpaint::operators::OpCtx ctx{
        W, H, pelpaint::operators::DrawMode::PixelPerfect};

    for (int i = 0; i < N; ++i) {
        // a. Apply palette rotation to the cycling layer.
        std::vector<Pixel> cycledPx;
        if (i == 0) {
            cycledPx = cycleBase;
        } else {
            auto op  = pelpaint::operators::pp_cycle_palette(
                           cfg.cycleColors, i, cfg.matchThreshold);
            auto res = op(std::span<const Pixel>{cycleBase}, ctx);
            if (!res) return std::unexpected(res.error());
            cycledPx = std::move(*res);
        }

        // b. Composite: static backdrop + cycled layer.
        std::vector<Pixel> composite = staticBase;
        AlphaOverAccum(composite, std::span<const Pixel>{cycledPx}, cycleLayer.opacity);

        // c. Append a new frame and write the composite.
        const int frameIdx = timeline.AddFrame();
        AnimationFrame& frame = timeline.Frame(frameIdx);
        frame.label = "Cycle " + std::to_string(i) + "/" + std::to_string(N - 1);
        if (cfg.frameDelay > 0.f)
            frame.delay = cfg.frameDelay;

        // Write the composited frame into the tiled surface via the canonical
        // WriteFlat path so this code stays in sync with ImageSurface internals.
        frame.surface.WriteFlat(std::span<const PixelRGBA8>{
            reinterpret_cast<const PixelRGBA8*>(composite.data()), composite.size()});

        // d. Record layer states for future re-baking / inspection.
        frame.layerStates.reserve(layers.size());
        for (int li : sortedIdx)
            frame.layerStates.push_back(
                {li,
                 li == cfg.cyclingLayerIdx ? true : layers[static_cast<std::size_t>(li)].visible,
                 li == cfg.cyclingLayerIdx ? cycleLayer.opacity
                                           : layers[static_cast<std::size_t>(li)].opacity});

        result.frameIndices.push_back(frameIdx);
    }

    return result;
}

} // namespace pelpaint::effects
