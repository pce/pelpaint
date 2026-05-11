#pragma once

/**
 * @file StepSequencer.hpp
 * @brief TB-303 style step sequencer.
 *
 * - Arbitrary length (1..kMaxSteps), default 16
 * - lastStep sets the loop point (independent of stepCount)
 * - Per-step: active, accent, glide, pitch (semitones), velocity
 * - Accent: 303-style — velocity boost + higher VCF cutoff peak
 *           + sharper/faster VCA envelope
 * - Glide: portamento from current pitch to next active step
 * - tick(dt) returns StepEvent when a new step fires
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <optional>

namespace pelpaint::audio {

/// @brief Per-step data for the step sequencer pattern.
struct Step {
    bool  active   = false;
    bool  accent   = false;   ///< 303-style accent
    bool  glide    = false;   ///< portamento TO the next active step
    int   pitch    = 0;       ///< semitones from rootPitch (-24..+24)
    float velocity = 0.75f;   ///< 0..1 (accent raises this)
    /// Oscillator wave shape for this step (maps to PolyBlepOscillator::Shape + Noise).
    /// 0=Saw, 1=Square, 2=Triangle, 3=Noise
    int   waveShape  = 0;
    /// Pulse width for waveShape==Square (0.05..0.95).
    float pulseWidth = 0.5f;
};

/// @brief Event fired to the voice engine when a step triggers.
struct StepEvent {
    bool  noteOn;     ///< false = rest / mute
    int   pitch;      ///< absolute MIDI note (rootPitch + step.pitch)
    float velocity;   ///< 0..1 (boosted for accent)
    bool  accent;     ///< drive VCF cutoff boost + sharper env
    bool  glide;      ///< slide from previous pitch to this one
    int   prevPitch;  ///< previous pitch (glide source)
    int   waveShape  = 0;    ///< propagated from Step::waveShape
    float pulseWidth = 0.5f; ///< propagated from Step::pulseWidth
};

/// @brief 303-style accent parameters (velocity, filter boost, envelope shaping).
struct Accent303Params {
    float velocityBoost      = 0.25f;    ///< added to velocity (clamped 0..1)
    float filterCutoffBoost  = 1200.f;   ///< Hz added to VCF base cutoff peak
    float attackTime         = 0.002f;   ///< sharper VCA attack (s) vs normal 0.01
    float decayFactor        = 0.55f;    ///< multiply normal decay (faster decay)
};

/**
 * @brief TB-303 style step sequencer.
 *
 * Drives voice triggers by advancing through a pattern of steps at a
 * BPM-derived rate. Call tick() once per audio block with the block's
 * duration in seconds; it returns a StepEvent when a new step fires.
 */
class StepSequencer {
public:
    static constexpr int kMaxSteps = 64;

    std::array<Step, kMaxSteps> steps{};
    int   stepCount = 16;   ///< number of steps in pattern (1..kMaxSteps)
    int   lastStep  = -1;   ///< loop point: -1 = use stepCount-1
    int   rootPitch = 48;   ///< MIDI note for pitch=0 (C3 = 48)
    float bpm       = 120.f;

    Accent303Params accentParams;
    float glideTime = 0.06f;   ///< portamento seconds

    bool  playing     = false;
    int   currentStep = 0;
    float elapsed     = 0.f;   ///< accumulator (seconds) within current step

    int   prevPitch_  = rootPitch;

    /// Effective loop length: lastStep+1 if set, else stepCount.
    [[nodiscard]] int loopLength() const noexcept {
        if (lastStep >= 0 && lastStep < stepCount)
            return lastStep + 1;
        return stepCount;
    }

    /// Duration of one step (16th note) in seconds.
    [[nodiscard]] float stepDuration() const noexcept {
        return 60.f / (bpm * 4.f);   // 4 steps per beat → 16th notes
    }

    /// Find the next active step after `from` (wraps within loop).
    [[nodiscard]] int nextActiveStep(int from) const noexcept {
        const int len = loopLength();
        for (int i = 1; i <= len; ++i) {
            const int idx = (from + i) % len;
            if (steps[idx].active) return idx;
        }
        return from;   // no other active step found
    }

    void play()  noexcept { playing = true; }
    void stop()  noexcept { playing = false; elapsed = 0.f; }
    void rewind() noexcept { currentStep = 0; elapsed = 0.f; prevPitch_ = rootPitch; }

    /// Advance by `dt` seconds.
    /// Returns a StepEvent when a step fires, std::nullopt otherwise.
    [[nodiscard]] std::optional<StepEvent> tick(float dt) noexcept {
        if (!playing) return std::nullopt;

        elapsed += dt;
        const float dur = stepDuration();
        if (elapsed < dur) return std::nullopt;

        // Allow for skipped steps (very large dt)
        elapsed = std::fmod(elapsed, dur);

        const int idx        = currentStep % loopLength();
        const Step& s        = steps[idx];

        StepEvent ev;
        ev.noteOn   = s.active;
        ev.pitch    = rootPitch + s.pitch;
        ev.accent   = s.active && s.accent;
        ev.prevPitch = prevPitch_;

        // Glide: the PREVIOUS step had glide set toward this one
        // (303 behaviour: glide flag on a step means "slide INTO next step")
        // We store a glide-pending flag and deliver it on the next noteOn.
        ev.glide = pendingGlide_ && s.active;
        pendingGlide_ = s.active && s.glide;
        ev.waveShape  = s.waveShape;
        ev.pulseWidth = s.pulseWidth;

        if (s.active) {
            float vel = s.velocity;
            if (s.accent)
                vel = std::min(1.f, vel + accentParams.velocityBoost);
            ev.velocity = vel;
            prevPitch_  = ev.pitch;
        } else {
            ev.velocity = 0.f;
        }

        // Advance step counter
        currentStep = (currentStep + 1) % loopLength();

        return ev;
    }

private:
    bool pendingGlide_ = false;   ///< glide flag set by previous step
};

/**
 * @brief Step-level arpeggiator: cycles through a chord at each note trigger.
 *
 * Call apply() when processing a StepEvent to replace the raw pitch with the
 * current arpeggio position.  Call reset() when the sequencer rewinds.
 *
 * @par Chord format
 *   intervals[] holds semitone offsets from the step's root pitch.
 *   e.g. {0, 4, 7} = major triad; {0, 3, 7} = minor triad.
 */
struct Arpeggiator {
    enum class Mode { Off, Up, Down, UpDown, Random };

    Mode  mode      = Mode::Off;
    int   octaves   = 1;                  ///< number of octaves to span (1..4)
    std::array<float, 8> intervals{};     ///< semitone offsets (up to 8 notes)
    int   size      = 0;                  ///< how many intervals are active

    /// Set a common chord pattern.  Clears existing intervals.
    void setChord(std::initializer_list<float> semitones) noexcept {
        size = 0;
        for (float s : semitones) {
            if (size >= static_cast<int>(intervals.size())) break;
            intervals[static_cast<std::size_t>(size++)] = s;
        }
    }

    /// Apply arpeggio to @p basePitch, returning the arpeggiated MIDI note.
    /// @note Call once per step trigger. Returns basePitch unchanged when Off or size==0.
    [[nodiscard]] int apply(int basePitch) noexcept {
        if (mode == Mode::Off || size <= 0) return basePitch;

        const int total  = size * std::max(1, octaves);
        const int oct    = step_ / size;
        const int idx    = step_ % size;
        const int result = basePitch
                         + static_cast<int>(intervals[static_cast<std::size_t>(idx)])
                         + oct * 12;

        // Advance
        switch (mode) {
            case Mode::Up:
                step_ = (step_ + 1) % total;
                break;
            case Mode::Down:
                step_ = (step_ - 1 + total) % total;
                break;
            case Mode::UpDown:
                step_ += dir_;
                if (step_ >= total - 1) { step_ = total - 1; dir_ = -1; }
                if (step_ <= 0)         { step_ = 0;          dir_ = +1; }
                break;
            case Mode::Random:
                // Simple LCG — good enough for musical randomness.
                rng_ = rng_ * 1664525u + 1013904223u;
                step_ = static_cast<int>((rng_ >> 16) % static_cast<std::uint32_t>(total));
                break;
            default: break;
        }

        return result;
    }

    void reset() noexcept { step_ = 0; dir_ = 1; }

private:
    int           step_ = 0;
    int           dir_  = 1;
    std::uint32_t rng_  = 0xDEADBEEFu;
};

} // namespace pelpaint::audio
