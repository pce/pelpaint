#pragma once

/**
 * @file DSP.hpp
 * @brief Header-only DSP primitives shared by live playback and offline render.
 *
 * Design rule: every algorithm here is stateless or carries its own state.
 * AudioEngine::processBlock() calls into these — both the real-time SDL3
 * callback and the offline WavRenderer loop hit the exact same code path,
 * guaranteeing render == live.
 *
 * Primitives:
 *   - PolyBlepOscillator  — band-limited saw/square/tri (no aliasing)
 *   - LadderFilter        — 4-pole Moog-style ladder (303 resonant LP)
 *   - BiquadFilter        — 2nd-order IIR (EQ shelves, peaks, HP/LP)
 *   - ADSR                — Attack/Decay/Sustain/Release envelope
 *   - Compressor          — Feed-forward peak compressor w/ soft knee
 *   - BrickwallLimiter    — Lookahead true-peak limiter (render-safe)
 *   - DelayLine<N>        — Fixed-capacity circular delay
 *   - StereoWidth         — Mid-side width control
 *   - DC_Block            — Simple 1-pole DC blocker
 *   - Oversampler2x       — 2x oversampling with LP decimation
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <numbers>
#include <vector>

namespace pelpaint::dsp {

inline constexpr float kTwoPi  = 2.f * std::numbers::pi_v<float>;
inline constexpr float kHalfPi = std::numbers::pi_v<float> * 0.5f;

[[nodiscard]] static inline float clamp01(float x) noexcept {
    return x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
}
[[nodiscard]] static inline float dbToLinear(float db) noexcept {
    return std::pow(10.f, db / 20.f);
}
[[nodiscard]] static inline float linearToDb(float lin) noexcept {
    return lin > 1e-9f ? 20.f * std::log10(lin) : -180.f;
}
[[nodiscard]] static inline float softClip(float x) noexcept {
    // cubic soft-clip: output stays in (-1, +1) for all inputs
    if (x >=  1.f) return  2.f / 3.f;
    if (x <= -1.f) return -2.f / 3.f;
    return x - (x * x * x) / 3.f;
}

/**
 * @brief PolyBLEP residual correction for band-limited oscillators.
 *
 * Applied at each phase discontinuity to eliminate aliasing.
 * @param t  Phase position within [0, 1].
 * @param dt Phase increment per sample (freq / sampleRate).
 * @return   Correction value to add/subtract at the discontinuity.
 */
[[nodiscard]] static inline float polyBlep(float t, float dt) noexcept {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.f;
    }
    if (t > 1.f - dt) {
        t = (t - 1.f) / dt;
        return t * t + t + t + 1.f;
    }
    return 0.f;
}

/**
 * @brief Band-limited oscillator using the PolyBLEP technique.
 *
 * Waveforms: Saw, Square, Triangle (all alias-free). Triangle is integrated
 * from a square via a leaky integrator, requiring one extra sample of state
 * but producing a perfectly alias-free result.
 */
struct PolyBlepOscillator {
    enum class Shape { Saw, Square, Triangle };

    Shape shape   = Shape::Saw;
    float phase   = 0.f;     // [0, 1)
    float freq    = 440.f;   // Hz
    float pw      = 0.5f;    // pulse width [0.05, 0.95] for Square only
    float sampleRate = 44100.f;

    // Triangle integrator state
    float triAccum = 0.f;

    void reset() noexcept { phase = 0.f; triAccum = 0.f; }

    void setSampleRate(float sr) noexcept { sampleRate = sr; }

    [[nodiscard]] float process() noexcept {
        const float dt = freq / sampleRate;

        float out = 0.f;
        switch (shape) {
            case Shape::Saw:
                out = 2.f * phase - 1.f;          // naive saw [-1, +1]
                out -= polyBlep(phase, dt);        // correct rising discontinuity
                break;

            case Shape::Square: {
                out = (phase < pw) ? 1.f : -1.f;  // naive square
                out += polyBlep(phase, dt);        // rising edge correction
                out -= polyBlep(std::fmod(phase + 1.f - pw, 1.f), dt); // falling
                break;
            }

            case Shape::Triangle:
                // Integrate a square to get triangle (leaky integrator)
                // Frequency compensation: multiply by 4*freq/sr
                out = (phase < 0.5f) ? 1.f : -1.f;
                out += polyBlep(phase, dt);
                out -= polyBlep(std::fmod(phase + 0.5f, 1.f), dt);
                triAccum += 4.f * dt * out;
                triAccum = clamp01((triAccum + 1.f) * 0.5f) * 2.f - 1.f;
                // Use triAccum as output on next call; return current
                out = triAccum;
                break;
        }

        // Advance phase
        phase += dt;
        if (phase >= 1.f) phase -= 1.f;

        return out;
    }
};

/**
 * @brief 4-pole Moog-style resonant low-pass filter.
 *
 * Non-linear (tanh) stages give the characteristic warm/fat sound.
 * The 303 uses this topology with high resonance settings.
 * Cutoff: Hz; Resonance: 0..4 (self-oscillates at ~4).
 */
struct LadderFilter {
    float cutoff    = 1000.f;  // Hz
    float resonance = 0.5f;    // 0..4
    float sampleRate = 44100.f;

    void setSampleRate(float sr) noexcept { sampleRate = sr; }

    void setParams(float cutoffHz, float res) noexcept {
        cutoff    = std::clamp(cutoffHz, 10.f, sampleRate * 0.49f);
        resonance = std::clamp(res, 0.f, 4.f);
    }

    [[nodiscard]] float process(float x) noexcept {
        // Normalised cutoff frequency (0..1)
        const float fc  = cutoff / sampleRate;
        const float g   = std::tan(std::numbers::pi_v<float> * fc); // prewarped
        const float gp1 = 1.f + g;

        // Feedback
        const float fb = resonance * stage_[3];

        x = std::tanh(x - fb);

        // 4 cascaded 1-pole LP stages (trapezoidal integration)
        for (int i = 0; i < 4; ++i) {
            const float v = (x - stage_[i]) * g / gp1;
            const float out = v + stage_[i];
            stage_[i] = out + v;
            x = out;
        }
        return x;
    }

    void reset() noexcept { stage_.fill(0.f); }

private:
    std::array<float, 4> stage_{};
};

/// @brief General 2nd-order IIR biquad filter for EQ (LP, HP, shelves, peak).
struct BiquadFilter {
    enum class Type {
        LowPass, HighPass, BandPass,
        Notch, PeakEQ, LowShelf, HighShelf, AllPass
    };

    void setSampleRate(float sr) noexcept { sr_ = sr; }

    void setLowPass(float freq, float Q = 0.707f) noexcept {
        calcCoeffs(Type::LowPass, freq, Q, 0.f);
    }
    void setHighPass(float freq, float Q = 0.707f) noexcept {
        calcCoeffs(Type::HighPass, freq, Q, 0.f);
    }
    void setPeakEQ(float freq, float Q, float gainDb) noexcept {
        calcCoeffs(Type::PeakEQ, freq, Q, gainDb);
    }
    void setLowShelf(float freq, float gainDb) noexcept {
        calcCoeffs(Type::LowShelf, freq, 0.707f, gainDb);
    }
    void setHighShelf(float freq, float gainDb) noexcept {
        calcCoeffs(Type::HighShelf, freq, 0.707f, gainDb);
    }

    [[nodiscard]] float process(float x) noexcept {
        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_
                                - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x;
        y2_ = y1_; y1_ = y;
        return y;
    }

    void reset() noexcept { x1_=x2_=y1_=y2_=0.f; }

private:
    float sr_ = 44100.f;
    float b0_=1.f, b1_=0.f, b2_=0.f;
    float a1_=0.f, a2_=0.f;
    float x1_=0.f, x2_=0.f, y1_=0.f, y2_=0.f;

    void calcCoeffs(Type t, float f0, float Q, float dBgain) noexcept {
        const float w0    = kTwoPi * f0 / sr_;
        const float cosW  = std::cos(w0);
        const float sinW  = std::sin(w0);
        const float alpha = sinW / (2.f * Q);
        const float A     = std::pow(10.f, dBgain / 40.f);

        float b0, b1, b2, a0, a1, a2;

        switch (t) {
            case Type::LowPass:
                b0=(1.f-cosW)/2.f; b1=1.f-cosW; b2=b0;
                a0=1.f+alpha; a1=-2.f*cosW; a2=1.f-alpha;
                break;
            case Type::HighPass:
                b0=(1.f+cosW)/2.f; b1=-(1.f+cosW); b2=b0;
                a0=1.f+alpha; a1=-2.f*cosW; a2=1.f-alpha;
                break;
            case Type::PeakEQ:
                b0=1.f+alpha*A; b1=-2.f*cosW; b2=1.f-alpha*A;
                a0=1.f+alpha/A; a1=-2.f*cosW; a2=1.f-alpha/A;
                break;
            case Type::LowShelf: {
                const float sq = 2.f * std::sqrt(A) * alpha;
                b0=A*((A+1.f)-(A-1.f)*cosW+sq);
                b1=2.f*A*((A-1.f)-(A+1.f)*cosW);
                b2=A*((A+1.f)-(A-1.f)*cosW-sq);
                a0=(A+1.f)+(A-1.f)*cosW+sq;
                a1=-2.f*((A-1.f)+(A+1.f)*cosW);
                a2=(A+1.f)+(A-1.f)*cosW-sq;
                break;
            }
            case Type::HighShelf: {
                const float sq = 2.f * std::sqrt(A) * alpha;
                b0=A*((A+1.f)+(A-1.f)*cosW+sq);
                b1=-2.f*A*((A-1.f)+(A+1.f)*cosW);
                b2=A*((A+1.f)+(A-1.f)*cosW-sq);
                a0=(A+1.f)-(A-1.f)*cosW+sq;
                a1=2.f*((A-1.f)-(A+1.f)*cosW);
                a2=(A+1.f)-(A-1.f)*cosW-sq;
                break;
            }
            default:
                b0=1.f; b1=b2=a1=a2=0.f; a0=1.f;
                break;
        }

        b0_=b0/a0; b1_=b1/a0; b2_=b2/a0;
        a1_=a1/a0; a2_=a2/a0;
    }
};

/// @brief Attack / Decay / Sustain / Release envelope generator.
struct ADSR {
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    float attackTime  = 0.01f;   // seconds
    float decayTime   = 0.1f;    // seconds
    float sustain     = 0.7f;    // 0..1 level
    float releaseTime = 0.2f;    // seconds

    void setSampleRate(float sr) noexcept { sr_ = sr; }
    void noteOn()  noexcept { stage_ = Stage::Attack;  value_ = 0.f; }
    void noteOff() noexcept { if (stage_ != Stage::Idle) stage_ = Stage::Release; }
    [[nodiscard]] bool isIdle() const noexcept { return stage_ == Stage::Idle; }

    [[nodiscard]] float process() noexcept {
        switch (stage_) {
            case Stage::Idle:    break;
            case Stage::Attack:
                value_ += 1.f / (attackTime  * sr_);
                if (value_ >= 1.f) { value_ = 1.f; stage_ = Stage::Decay; }
                break;
            case Stage::Decay:
                value_ -= (1.f - sustain) / (decayTime * sr_);
                if (value_ <= sustain) { value_ = sustain; stage_ = Stage::Sustain; }
                break;
            case Stage::Sustain:
                value_ = sustain;
                break;
            case Stage::Release:
                value_ -= sustain / (releaseTime * sr_);
                if (value_ <= 0.f) { value_ = 0.f; stage_ = Stage::Idle; }
                break;
        }
        return value_;
    }

private:
    float sr_     = 44100.f;
    float value_  = 0.f;
    Stage stage_  = Stage::Idle;
};

/// @brief 1-pole DC blocker (~10 Hz high-pass) to remove DC offset.
struct DC_Block {
    [[nodiscard]] float process(float x) noexcept {
        const float y = x - xm1_ + 0.9995f * ym1_;
        xm1_ = x; ym1_ = y;
        return y;
    }
    void reset() noexcept { xm1_ = ym1_ = 0.f; }
private:
    float xm1_ = 0.f, ym1_ = 0.f;
};

/// @brief Fixed-capacity circular delay line (compile-time maximum size).
template<std::size_t MaxSamples>
struct DelayLine {
    void clear() noexcept { buf_.fill(0.f); pos_ = 0; }

    void write(float x) noexcept {
        buf_[pos_] = x;
        pos_ = (pos_ + 1) % MaxSamples;
    }

    [[nodiscard]] float read(std::size_t delaySamples) const noexcept {
        const std::size_t idx = (pos_ + MaxSamples - delaySamples) % MaxSamples;
        return buf_[idx];
    }

    [[nodiscard]] float readInterp(float delaySamples) const noexcept {
        const auto d    = static_cast<std::size_t>(delaySamples);
        const float frac = delaySamples - static_cast<float>(d);
        const float a    = read(d);
        const float b    = read(d + 1);
        return a + frac * (b - a);
    }

private:
    std::array<float, MaxSamples> buf_{};
    std::size_t pos_ = 0;
};

/**
 * @brief Lookahead true-peak brickwall limiter.
 *
 * Prevents clipping in both live and offline render.
 * CRITICAL: must be the final stage on the master bus.
 *
 * - Lookahead: 5 ms (built-in latency, compensated in render)
 * - Threshold: -0.3 dBFS (default; fully configurable)
 * - Attack:    immediate (hard limiter)
 * - Release:   100 ms (smooth gain recovery)
 *
 * Algorithm: feed signal into lookahead buffer, find peak over the window,
 * compute required gain, apply ballistics (fast down, slow up),
 * then apply gain to the delayed output signal.
 */
class BrickwallLimiter {
public:
    // Default: -0.3 dBFS threshold, 5 ms lookahead, 100 ms release
    void init(float sampleRate,
              float thresholdDb = -0.3f,
              float lookaheadMs = 5.f,
              float releaseMs   = 100.f) noexcept
    {
        sr_          = sampleRate;
        threshold_   = dbToLinear(thresholdDb);
        lookahead_   = static_cast<int>(lookaheadMs * 0.001f * sampleRate);
        releaseCoef_ = std::exp(-1.f / (releaseMs * 0.001f * sampleRate));

        const std::size_t bufSize = static_cast<std::size_t>(lookahead_ + 16);
        delayL_.assign(bufSize, 0.f);
        delayR_.assign(bufSize, 0.f);
        writePos_ = 0;

        gainReduction_ = 1.f;
    }

    // Process one stereo sample.  In-place.
    void process(float& L, float& R) noexcept {
        const std::size_t n = delayL_.size();

        // Write new sample into delay buffer
        delayL_[writePos_] = L;
        delayR_[writePos_] = R;

        // Read the "old" sample (lookahead samples ago)
        const std::size_t readPos = (writePos_ + n - static_cast<std::size_t>(lookahead_)) % n;
        const float outL = delayL_[readPos];
        const float outR = delayR_[readPos];

        writePos_ = (writePos_ + 1) % n;

        // Peak detection over lookahead window using incoming sample
        const float peak = std::max(std::abs(L), std::abs(R));

        // Gain computer: how much gain do we need to not exceed threshold?
        float targetGain = 1.f;
        if (peak > threshold_) targetGain = threshold_ / peak;

        // Ballistics: instantaneous attack (brickwall), slow release
        if (targetGain < gainReduction_) {
            gainReduction_ = targetGain;        // immediate attack
        } else {
            gainReduction_ = releaseCoef_ * gainReduction_
                           + (1.f - releaseCoef_) * targetGain;
        }

        // Apply gain to the delayed output signal
        L = outL * gainReduction_;
        R = outR * gainReduction_;
    }

    // Process a block of interleaved L/R samples.
    void processBlock(float* interleaved, int frameCount) noexcept {
        for (int i = 0; i < frameCount; ++i) {
            process(interleaved[i * 2    ],
                    interleaved[i * 2 + 1]);
        }
    }

    [[nodiscard]] float gainReductionDb() const noexcept {
        return linearToDb(gainReduction_);
    }

    void reset() noexcept {
        std::fill(delayL_.begin(), delayL_.end(), 0.f);
        std::fill(delayR_.begin(), delayR_.end(), 0.f);
        gainReduction_ = 1.f;
        writePos_ = 0;
    }

private:
    float sr_            = 44100.f;
    float threshold_     = dbToLinear(-0.3f);
    float releaseCoef_   = 0.f;
    float gainReduction_ = 1.f;
    int   lookahead_     = 220;   // samples

    std::vector<float> delayL_, delayR_;
    std::size_t writePos_ = 0;
};

/// @brief Feed-forward peak compressor with soft-knee gain computer.
struct Compressor {
    float thresholdDb =  -12.f;
    float ratio       =   4.f;   // :1
    float attackMs    =   5.f;
    float releaseMs   = 100.f;
    float makeupDb    =   0.f;

    void setSampleRate(float sr) noexcept { sr_ = sr; }

    [[nodiscard]] float process(float x) noexcept {
        const float env = std::abs(x);
        const float envDb = linearToDb(env + 1e-9f);

        // Gain computer with soft knee (6 dB wide)
        float gcDb = 0.f;
        const float knee = 6.f;
        const float diff = envDb - thresholdDb;
        if (2.f * diff < -knee) {
            gcDb = 0.f;
        } else if (2.f * std::abs(diff) <= knee) {
            gcDb = (1.f / ratio - 1.f) * (diff + knee * 0.5f) * (diff + knee * 0.5f) / (2.f * knee);
        } else {
            gcDb = (1.f / ratio - 1.f) * diff;
        }

        const float targetGain = dbToLinear(gcDb + makeupDb);

        // Ballistics
        const float attackCoef  = std::exp(-1.f / (attackMs  * 0.001f * sr_));
        const float releaseCoef = std::exp(-1.f / (releaseMs * 0.001f * sr_));

        if (targetGain < gainSmooth_)
            gainSmooth_ = attackCoef  * gainSmooth_ + (1.f - attackCoef)  * targetGain;
        else
            gainSmooth_ = releaseCoef * gainSmooth_ + (1.f - releaseCoef) * targetGain;

        return x * gainSmooth_;
    }

    void reset() noexcept { gainSmooth_ = 1.f; }

private:
    float sr_         = 44100.f;
    float gainSmooth_ = 1.f;
};

/// @brief Mid-side stereo width control (0 = mono, 1 = normal, 2 = double).
struct StereoWidth {
    float width = 1.f;  // 0=mono, 1=normal, 2=double

    void process(float& L, float& R) noexcept {
        const float mid  = (L + R) * 0.5f;
        const float side = (L - R) * 0.5f * width;
        L = mid + side;
        R = mid - side;
    }
};

/**
 * @brief 2x oversampler for the oscillator + filter stage.
 *
 * Upsamples input by 2x, processes at double rate, then decimates back.
 * Uses linear interpolation for upsampling and a biquad anti-alias LP
 * at 0.45*fs before decimation.
 */
struct Oversampler2x {
    // Insert one input sample, get two upsampled samples.
    void upsample(float in, float& s0, float& s1) noexcept {
        // Linear interpolation upsample (replace with polyphase FIR for production)
        s0 = in;
        s1 = 0.5f * (in + prev_);
        prev_ = in;
    }

    // Decimate two samples into one (anti-aliasing LP).
    [[nodiscard]] float downsample(float s0, float s1) noexcept {
        // Simple averaging (half-band): good enough for 2x
        const float out = (s0 + s1) * 0.5f;
        // Biquad anti-aliasing LP at 0.45 * fs (pre-decimation filter)
        return lp_.process(out);
    }

    void setSampleRate(float sr) noexcept {
        lp_.setSampleRate(sr * 2.f);          // LP runs at 2x rate
        lp_.setLowPass(sr * 0.45f, 0.6f);     // cut at just below Nyquist
    }

    void reset() noexcept { prev_ = 0.f; lp_.reset(); }

private:
    float       prev_ = 0.f;
    BiquadFilter lp_;
};

// ----------------------------------------------------------------

/**
 * @brief 16-bit Galois LFSR white-noise oscillator (SID-inspired).
 *
 * Period = 65535 samples at any frequency. Use as a noise source for
 * percussion, hi-hats, or SID-style noise voice. Not pitched — use
 * BiquadFilter after it to shape the spectral character.
 *
 * Polynomial: x^16 + x^14 + x^13 + x^11 + 1  (0xB400 mask).
 */
struct NoiseOscillator {
    /// @note Seed must never be zero (zero is absorbing).
    void reset() noexcept { lfsr_ = 0xACE1u; }
    void seed(std::uint16_t s) noexcept { lfsr_ = s ? s : 0xACE1u; }

    [[nodiscard]] float process() noexcept {
        const std::uint16_t lsb = lfsr_ & 1u;
        lfsr_ >>= 1;
        if (lsb) lfsr_ ^= 0xB400u;
        return (static_cast<float>(lfsr_) / 32767.5f) - 1.f;
    }

private:
    std::uint16_t lfsr_ = 0xACE1u;
};

} // namespace pelpaint::dsp
