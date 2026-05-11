#pragma once

#include <atomic>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/AnimationTimeline.hpp"
#include "../core/Canvas.hpp"
#include "../core/Error.hpp"
#include "../ColorPalettes.hpp"

namespace pelpaint::effects {

/// Portable cancellation token compatible with Emscripten and all desktop
/// targets.  Drop-in replacement for std::stop_token (same stop_requested()
/// API) without the C++20 <stop_token> / jthread dependency.
///
/// Default-constructed = never cancels.  To wire up cancellation:
///
///   auto flag = std::make_shared<std::atomic<bool>>(false);
///   BakeControl ctl{ CancelToken(flag) };
///   // ... later, from another thread:
///   flag->store(true);
///
class CancelToken {
public:
    CancelToken() = default;
    explicit CancelToken(std::shared_ptr<std::atomic<bool>> flag)
        : flag_(std::move(flag)) {}

    [[nodiscard]] bool stop_requested() const noexcept {
        return flag_ && flag_->load(std::memory_order_relaxed);
    }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

/// Cancellation and progress for long bakes.
/// Default-constructed = uncancellable, no progress callback.
struct BakeControl {
    CancelToken                   stopToken;            ///< default: never stops
    std::function<void(int,int)>  onProgress = nullptr; ///< (stepDone, total); nullptr = no-op
};

struct PaletteCycleConfig {
    /// Ordered list of colors that form the cycle group.
    /// N = size() = number of generated frames = cycle period.
    std::vector<Pixel> cycleColors;

    /// Index of the canvas layer whose colors will be cycled.
    int cyclingLayerIdx = -1;

    /// Quantize the cycling layer to cycleColors before generating frames.
    /// Recommended when the layer was painted with nearby (not exact) colors.
    bool quantizeFirst = false;

    /// Maximum Euclidean RGBA distance for a pixel to be treated as a
    /// member of the cycle group.  0.5 = near-exact; higher = tolerant.
    float matchThreshold = 0.5f;

    /// Per-frame delay (seconds).  0.0 = use the timeline's global FPS.
    float frameDelay = 0.f;
};

struct PaletteCycleResult {
    /// Timeline indices of the N generated frames (one per cycle step).
    std::vector<int> frameIndices;
    /// Equals cycleColors.size().
    int cycleLength = 0;
};

/** GeneratePaletteCycle
 *
 *   Bakes N frames into `timeline` that represent a complete palette cycle:
 *
 *   1. Composite all canvas layers EXCEPT the cycling layer  →  static backdrop.
 *   2. Optionally quantize the cycling layer to cycleColors.
 *   3. For each step i in [0, N):
 *        a.  Apply pp_cycle_palette(cycleColors, i) to the cycling layer pixels.
 *        b.  Alpha-composite the cycled result on top of the static backdrop.
 *        c.  Append a new AnimationFrame and write the composite into its surface.
 *        d.  Record FrameLayerState for every canvas layer (for future re-baking).
 *
 * Returns PaletteCycleResult on success; Error on bad inputs or alloc failure.
 */
[[nodiscard]] std::expected<PaletteCycleResult, pelpaint::Error>
GeneratePaletteCycle(
    pelpaint::core::AnimationTimeline& timeline,
    const pelpaint::Canvas&            canvas,
    const PaletteCycleConfig&          config,
    BakeControl                        ctl = {});

} // namespace pelpaint::effects
