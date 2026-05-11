#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ImageSurface.hpp"
#include "../operators/FrameOperator.hpp"
#include "../core/Error.hpp"

namespace pelpaint::core {

// ---------------------------------------------------------------------------
// FrameLayerState  —  keyframe record of one canvas layer's visibility for
//                     a single animation frame (Photoshop-style layer states).
// ---------------------------------------------------------------------------
struct FrameLayerState {
    int   layerIndex = -1;    ///< Index into Canvas::Layers()
    bool  visible    = true;  ///< Layer visibility override for this frame
    float opacity    = 1.0f;  ///< Layer opacity override (0..1)
};


/// @brief Wave shape for a frame's chip-tune audio trigger.
/// Mirrors DSP oscillator shapes without introducing an audio header dependency in core.
enum class ChipWave { Saw, Square, Triangle, Noise };

/// @brief Optional audio trigger attached to one animation frame.
///
/// When set, the export pipeline triggers a note at the start of this frame's
/// audio block. Ignored during normal rendering; used only by AnimExportPackage.
struct FrameAudioEvent {
    bool     active     = false;    ///< true = a note fires when this frame begins
    int      midiNote   = 60;       ///< MIDI note number (C4 = 60, range 0..127)
    float    velocity   = 0.75f;    ///< 0..1; boosted by accent
    bool     accent     = false;    ///< sharper attack + VCF cutoff boost
    bool     glide      = false;    ///< portamento from previous active frame's note
    ChipWave wave       = ChipWave::Square;
    float    pulseWidth = 0.5f;     ///< for Square wave only (0.05..0.95)
};

// AnimationFrame
//
// A single frame in an animation sequence.
//   • surface  — pixel content (composited flat image, same W×H as the canvas).
//   • delay    — per-frame duration override in seconds.
//               0.0 = use the timeline's global FPS.
//   • label    — optional display name for the strip UI.
// ---------------------------------------------------------------------------
struct AnimationFrame {
    ImageSurface surface;
    float        delay = 0.f;
    std::string  label;

    /// Per-frame layer visibility keyframes.
    /// Empty = "use the canvas layer defaults" (backward-compatible).
    /// Populated by GeneratePaletteCycle and similar effect bakers.
    std::vector<FrameLayerState> layerStates;

    // ---- Derived-frame / layer-state system (Photoshop-style) ----------
    //
    // If sourceFrame >= 0 this frame's pixels are produced by running
    // pipeline against frames_[sourceFrame].surface.  surface caches the
    // last baked result; call BakeFrame() to recompute after the source
    // or pipeline changes.
    int                                sourceFrame   = -1;   ///< -1 = direct (hand-edited)
    operators::DrawMode                mode          = operators::DrawMode::PixelPerfect;
    operators::FramePipeline           pipeline;             ///< ops applied to sourceFrame
    bool                               pipelineDirty = false;///< true -> BakeFrame() needed

    /// Optional audio trigger for this frame (used by AnimExportPackage).
    FrameAudioEvent audioEvent;
};


enum class AnimationPreset : int {
    Custom         = 0,
    Slideshow_1fps,
    Sprite_2fps,
    Sprite_4fps,
    Sprite_8fps,
    Sprite_12fps,
    GIF_10fps,
    GIF_15fps,
    Film_24fps,
    Video_30fps,
    Video_60fps,
    Count_          // sentinel — keep last
};

[[nodiscard]] constexpr float PresetToFPS(AnimationPreset p) noexcept {
    switch (p) {
        case AnimationPreset::Slideshow_1fps:  return  1.f;
        case AnimationPreset::Sprite_2fps:     return  2.f;
        case AnimationPreset::Sprite_4fps:     return  4.f;
        case AnimationPreset::Sprite_8fps:     return  8.f;
        case AnimationPreset::Sprite_12fps:    return 12.f;
        case AnimationPreset::GIF_10fps:       return 10.f;
        case AnimationPreset::GIF_15fps:       return 15.f;
        case AnimationPreset::Film_24fps:      return 24.f;
        case AnimationPreset::Video_30fps:     return 30.f;
        case AnimationPreset::Video_60fps:     return 60.f;
        default:                               return 12.f;
    }
}

[[nodiscard]] constexpr const char* PresetName(AnimationPreset p) noexcept {
    switch (p) {
        case AnimationPreset::Custom:          return "Custom";
        case AnimationPreset::Slideshow_1fps:  return "Slideshow  (1 fps)";
        case AnimationPreset::Sprite_2fps:     return "Sprite Idle (2 fps)";
        case AnimationPreset::Sprite_4fps:     return "Sprite Slow (4 fps)";
        case AnimationPreset::Sprite_8fps:     return "Sprite Classic (8 fps)";
        case AnimationPreset::Sprite_12fps:    return "Sprite Smooth (12 fps)";
        case AnimationPreset::GIF_10fps:       return "GIF Normal (10 fps)";
        case AnimationPreset::GIF_15fps:       return "GIF Fast (15 fps)";
        case AnimationPreset::Film_24fps:      return "Film / GIF HD (24 fps)";
        case AnimationPreset::Video_30fps:     return "Video (30 fps)";
        case AnimationPreset::Video_60fps:     return "Video HD (60 fps)";
        default:                               return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// AnimationTimeline
//
// Frame-based animation container with playback state machine.
//
//   ┌─────┬─────┬─────┬─────┬─────┐
//   │  0  │  1  │  2  │ ... │  N  │
//   └─────┴─────┴─────┴─────┴─────┘
//               ▲
//          currentFrame_
//
// • Update(dt) advances currentFrame_ based on per-frame delay or 1/fps_.
// • The timeline does NOT render anything; callers read Frame(CurrentFrame()).surface.
// • Looping wraps back to frame 0; non-looping stops at the last frame.
// ---------------------------------------------------------------------------
class AnimationTimeline {
public:
    enum class PlaybackState { Stopped, Playing, Paused };

    /// Construct with a fixed canvas size; always starts with one blank frame.
    AnimationTimeline(std::uint32_t width, std::uint32_t height);

    // ---- Frame management ----------------------------------------------

    /// Append a blank (transparent) frame. Returns the new frame index.
    int  AddFrame();

    /// Deep-copy frame at index and insert right after it. Returns new index.
    int  DuplicateFrame(int index);

    /// Insert a blank frame before the given index.
    void InsertFrame(int before);

    /// Remove frame at index. Always keeps at least one frame.
    void RemoveFrame(int index);

    /// Move frame from -> to, adjusting currentFrame_ accordingly.
    void MoveFrame(int from, int to);

    /// Create a new frame derived from sourceIndex by applying pipeline.
    /// The new frame is appended at the end.  Call BakeFrame(newIdx) to
    /// populate its surface.  Returns the new frame's index.
    int DeriveFrame(int                              sourceIndex,
                    operators::FramePipeline         pipeline,
                    operators::DrawMode              mode =
                        operators::DrawMode::PixelPerfect);

    /// Re-apply the stored pipeline from sourceFrame into frame.surface.
    /// No-op (success) when sourceFrame < 0 or pipeline is empty.
    [[nodiscard]] std::expected<void, pelpaint::Error> BakeFrame(int frameIndex);

    /// Bake every derived frame marked pipelineDirty.
    void BakeAllDirty();

    /// Mark every frame that references sourceIndex as pipelineDirty.
    void MarkDependentsDirty(int sourceIndex) noexcept;

    [[nodiscard]] int                   FrameCount() const noexcept;
    [[nodiscard]] AnimationFrame&       Frame(int i);
    [[nodiscard]] const AnimationFrame& Frame(int i) const;

    // ---- Playback ------------------------------------------------------

    void Play();
    void Pause();
    void Stop();

    /// Advance the animation by dt seconds; changes currentFrame_ as needed.
    void Update(float dt);

    [[nodiscard]] PlaybackState State()        const noexcept { return state_;        }
    [[nodiscard]] int           CurrentFrame() const noexcept { return currentFrame_; }
    void                        SetCurrentFrame(int i) noexcept;

    // ---- Settings ------------------------------------------------------

    [[nodiscard]] float FPS()     const noexcept { return fps_;     }
    void                SetFPS(float fps) noexcept;

    [[nodiscard]] bool  Looping() const noexcept { return looping_; }
    void                SetLooping(bool v) noexcept { looping_ = v; }

    [[nodiscard]] operators::DrawMode DefaultMode() const noexcept { return defaultMode_; }
    void SetDefaultMode(operators::DrawMode m) noexcept { defaultMode_ = m; }

    /// Total animation duration in seconds (sum of all effective per-frame delays).
    [[nodiscard]] float TotalDuration() const;

    /// Effective delay for frame i (per-frame override or 1/fps_).
    [[nodiscard]] float FrameDelay(int i) const noexcept;

    // ---- Dimensions ----------------------------------------------------

    [[nodiscard]] std::uint32_t Width()  const noexcept { return width_;  }
    [[nodiscard]] std::uint32_t Height() const noexcept { return height_; }

    /// Resize all frames (clears pixel data in each frame's surface).
    void Resize(std::uint32_t newW, std::uint32_t newH);

private:
    std::uint32_t               width_;
    std::uint32_t               height_;
    std::vector<AnimationFrame> frames_;
    float                       fps_          = 12.f;
    bool                        looping_      = true;
    PlaybackState               state_        = PlaybackState::Stopped;
    int                         currentFrame_ = 0;
    float                       elapsed_      = 0.f;   // accumulator for current frame
    operators::DrawMode         defaultMode_  = operators::DrawMode::PixelPerfect;
};

} // namespace pelpaint::core
