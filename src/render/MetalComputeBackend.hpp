#pragma once

// GPU minimum-pixel thresholds (approximate):
//   Unified memory (Apple Silicon, A15+)  →  4 096  (64×64)
//   Non-unified iOS/iPadOS (A9–A14)       → 16 384  (128×128)
//   Non-unified macOS (Intel+discrete)    → 16 384  (128×128)

#include "ComputeBackend.hpp"
#include <memory>

namespace pelpaint::render {

class MetalComputeBackend : public BackendMixin {
public:
    /// If metalDevice is nil or Metal init fails, Capabilities().hasGpu is false
    /// and Backend::Create() falls through to CpuComputeBackend.
    explicit MetalComputeBackend(void* metalDevice);

    // User-defined destructor (PIMPL) suppresses implicit move — declare explicitly
    // so std::movable<MetalComputeBackend> is satisfied for BackendConcept.
    ~MetalComputeBackend();
    MetalComputeBackend(MetalComputeBackend&&) noexcept;
    MetalComputeBackend& operator=(MetalComputeBackend&&) noexcept;

    MetalComputeBackend(const MetalComputeBackend&)            = delete;
    MetalComputeBackend& operator=(const MetalComputeBackend&) = delete;

    [[nodiscard]] ComputeCapabilities Capabilities() const noexcept;

    [[nodiscard]] std::expected<effects::PaletteCycleResult, Error>
    BakePaletteCycleFrames(core::AnimationTimeline&           timeline,
                            const Canvas&                      canvas,
                            const effects::PaletteCycleConfig& cfg,
                            effects::BakeControl               ctl = {});

    [[nodiscard]] std::expected<std::vector<Pixel>, Error>
    ApplyCyclePalette(std::span<const Pixel> src,
                      int w, int h,
                      std::span<const Pixel> cycleColors,
                      int                    offset,
                      float                  matchThreshold);

    [[nodiscard]] std::expected<std::vector<Pixel>, Error>
    ApplyGrayscale(std::span<const Pixel> src);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pelpaint::render
