#pragma once

#include "ComputeBackend.hpp"
#include "../operators/FrameOperator.hpp"
#include "../operators/pp_cycle_palette.hpp"
#include "../filters/Filters.hpp"
#include "../effects/PaletteCycler.hpp"

namespace pelpaint::render {

class CpuComputeBackend : public BackendMixin {
public:
    [[nodiscard]] ComputeCapabilities Capabilities() const noexcept
    {
        return {
            BackendCapability::None,
            /*hasGpu=*/       false,
            /*gpuMinPixels=*/ 0,
            "CPU (software renderer)"
        };
    }

    [[nodiscard]] std::expected<effects::PaletteCycleResult, Error>
    BakePaletteCycleFrames(core::AnimationTimeline&           timeline,
                            const Canvas&                      canvas,
                            const effects::PaletteCycleConfig& cfg)
    {
        return effects::GeneratePaletteCycle(timeline, canvas, cfg);
    }

    [[nodiscard]] std::expected<std::vector<Pixel>, Error>
    ApplyCyclePalette(std::span<const Pixel> src,
                      int w, int h,
                      std::span<const Pixel> cycleColors,
                      int                    offset,
                      float                  matchThreshold)
    {
        using namespace pelpaint::operators;
        if (cycleColors.empty())
            return std::vector<Pixel>(src.begin(), src.end());

        const auto op = pp_cycle_palette(
            std::vector<Pixel>(cycleColors.begin(), cycleColors.end()),
            offset,
            matchThreshold);
        const OpCtx ctx{w, h, DrawMode::PixelPerfect};
        return op(src, ctx);
    }

    [[nodiscard]] std::expected<std::vector<Pixel>, Error>
    ApplyGrayscale(std::span<const Pixel> src)
    {
        return filters::ToGrayscale(src);
    }
};

} // namespace pelpaint::render
