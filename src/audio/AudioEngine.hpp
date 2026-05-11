#pragma once

/**
 * @file AudioEngine.hpp
 * @brief Central audio graph.
 */

#include "DSP.hpp"
#include "StepSequencer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace pelpaint::audio {

/// @brief Convert a MIDI note number to frequency in Hz.
[[nodiscard]] static inline float midiToHz(float note) noexcept {
    return 440.f * std::pow(2.f, (note - 69.f) / 12.f);
}

/**
 * @brief One 303-style synthesis voice.
 *
 * Signal path (per sample): PolyBLEP saw -> 2x oversample
 * -> LadderFilter -> VCA envelope -> DC blocker -> output.
 *
 * Glide: pitch slides from prevHz to targetHz over glideTime seconds.
 * Accent: sharper VCA attack + higher VCF cutoff peak + more velocity.
 */
struct Voice303 {
    dsp::PolyBlepOscillator osc;
    dsp::LadderFilter       vcf;
    dsp::ADSR               vcaEnv;
    dsp::ADSR               vcfEnv;   // modulates VCF cutoff
    dsp::DC_Block           dc;

    bool  active      = false;
    float pitchHz     = 440.f;   // current pitch (slides during glide)
    float targetHz    = 440.f;
    float glideRate   = 0.f;     // Hz change per sample (signed)
    bool  gliding     = false;

    float vcfCutoffBase = 400.f;
    float vcfCutoffEnvAmt = 2000.f;  // Hz added at peak of vcfEnv
    float vcfCutoffAccBoost = 1200.f;
    float resonance    = 0.6f;
    bool  accented     = false;

    dsp::NoiseOscillator noise_;          ///< LFSR noise source (waveShape==3)
    int   waveShape_   = 0;               ///< 0=Saw, 1=Square, 2=Triangle, 3=Noise

    void init(float sampleRate) {
        osc.setSampleRate(sampleRate);
        vcf.setSampleRate(sampleRate);
        vcaEnv.setSampleRate(sampleRate);
        vcfEnv.setSampleRate(sampleRate);
        noise_.reset();
    }

    void noteOn(const StepEvent& ev,
                float sampleRate,
                float cutoffHz,
                float res,
                float vcaSustain,
                float vcaDecay,
                const Accent303Params& ap) noexcept
    {
        active      = true;
        accented    = ev.accent;
        waveShape_  = 0;  // default; caller (triggerVoice) overrides after noteOn
        resonance = res;
        vcfCutoffBase    = cutoffHz;
        vcfCutoffAccBoost = ap.filterCutoffBoost;

        // Pitch / glide
        targetHz = midiToHz(static_cast<float>(ev.pitch));
        if (ev.glide && sampleRate > 0.f) {
            // Slide from previous pitch
            const float fromHz = midiToHz(static_cast<float>(ev.prevPitch));
            pitchHz   = fromHz;
            glideRate = (targetHz - fromHz) / (ap.decayFactor == 0 ? 1.f :
                          sampleRate * 0.06f); // glideTime ≈ 60ms
            gliding   = true;
        } else {
            pitchHz   = targetHz;
            glideRate = 0.f;
            gliding   = false;
            osc.reset();
            noise_.reset();  // re-seed noise on each new note
        }

        // VCA envelope — accent makes it sharper
        vcaEnv.attackTime  = ev.accent ? ap.attackTime : 0.010f;
        vcaEnv.decayTime   = ev.accent ? (vcaDecay * ap.decayFactor)
                                       : vcaDecay;
        vcaEnv.sustain     = vcaSustain;
        vcaEnv.releaseTime = 0.05f;
        vcaEnv.noteOn();

        // VCF envelope
        vcfEnv.attackTime  = ev.accent ? 0.002f : 0.006f;
        vcfEnv.decayTime   = ev.accent ? 0.08f : 0.25f;
        vcfEnv.sustain     = 0.f;
        vcfEnv.releaseTime = 0.05f;
        vcfEnv.noteOn();

        vcf.reset();
    }

    void noteOff() noexcept {
        vcaEnv.noteOff();
        vcfEnv.noteOff();
        gliding = false;
    }

    [[nodiscard]] bool isIdle() const noexcept { return vcaEnv.isIdle(); }

    // Returns one mono sample.
    [[nodiscard]] float processSample() noexcept {
        if (isIdle()) { active = false; return 0.f; }

        // Glide
        if (gliding) {
            pitchHz += glideRate;
            if ((glideRate > 0.f && pitchHz >= targetHz) ||
                (glideRate < 0.f && pitchHz <= targetHz) ||
                (glideRate == 0.f)) {
                pitchHz = targetHz;
                gliding = false;
            }
        }

        osc.freq = pitchHz;

        // VCF modulation (env + accent boost)
        const float vcfMod  = vcfEnv.process() * vcfCutoffEnvAmt;
        const float acBoost = accented ? vcfCutoffAccBoost : 0.f;
        vcf.setParams(vcfCutoffBase + vcfMod + acBoost, resonance);

        // VCA envelope
        const float vca = vcaEnv.process();

        // OSC → VCF → VCA → DC block
        // waveShape_ == 3 → LFSR noise (SID drum/perc voice)
        const float raw = (waveShape_ == 3)
            ? noise_.process()
            : osc.process();
        const float filt = vcf.process(raw);
        const float out  = dc.process(filt * vca);

        return out;
    }
};

/**
 * @brief Central audio processing engine.
 *
 * @invariant processBlock() is the ONLY audio processing entry point.
 *  Both the SDL3 live callback and WavRenderer call this function.
 *  There is no render-mode bypass: render == live, always.
 */
class AudioEngine {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kBlockSize  = 256;    // frames per processBlock call
    static constexpr int kNumVoices  = 4;      // 303-style: small pool for legato

    StepSequencer sequencer;
    Arpeggiator   arp;  ///< optional arpeggiator applied to sequencer note events

    // Channel EQ (applied to voice mix before master)
    struct ChannelEQParams {
        bool  enabled   = false;
        float lowGainDb  = 0.f;  // low shelf  @ 200 Hz
        float midGainDb  = 0.f;  // peak EQ    @ 1000 Hz, Q=1
        float highGainDb = 0.f;  // high shelf @ 8000 Hz
    } channelEQ;

    // Master bus params
    struct MasterParams {
        float gainDb          = 0.f;     // master output level
        float compThresholdDb = -18.f;
        float compRatio       = 2.5f;
        float stereoWidth     = 1.f;
        float limiterThreshDb = -0.3f;   // brick-wall ceiling
    } master;

    // Voice-level synth params (sequencer maps these at noteOn)
    float vcfCutoffHz   = 500.f;
    float vcfResonance  = 0.55f;
    float vcaSustainLvl = 0.f;    // 303 has near-zero sustain (decay only)
    float vcaDecaySecs  = 0.35f;

    void init(float sampleRate = static_cast<float>(kSampleRate)) {
        sampleRate_ = sampleRate;

        for (auto& v : voices_) v.init(sampleRate);

        // Channel EQ
        eqLow_.setSampleRate(sampleRate);
        eqLow_.setLowShelf(200.f, 0.f);
        eqMid_.setSampleRate(sampleRate);
        eqMid_.setPeakEQ(1000.f, 1.f, 0.f);
        eqHigh_.setSampleRate(sampleRate);
        eqHigh_.setHighShelf(8000.f, 0.f);

        // Master EQ (gentle warmth/air)
        masterLow_.setSampleRate(sampleRate);
        masterLow_.setLowShelf(80.f, 0.f);
        masterHigh_.setSampleRate(sampleRate);
        masterHigh_.setHighShelf(12000.f, 0.f);

        // Master compressor
        compressor_.setSampleRate(sampleRate);
        compressor_.thresholdDb = master.compThresholdDb;
        compressor_.ratio       = master.compRatio;
        compressor_.attackMs    = 20.f;
        compressor_.releaseMs   = 150.f;
        compressor_.makeupDb    = 0.f;

        // Limiter — always on, identical for live and render
        limiter_.init(sampleRate, master.limiterThreshDb, 5.f, 80.f);

        mixBuf_.resize(static_cast<std::size_t>(kBlockSize * 2), 0.f);
    }

    /**
     * @brief Process one block of audio. Called identically by live and render paths.
     *
     * @par Signal chain
     *   StepSequencer.tick() (events)
     *   -> VoicePool (Voice303 x 4): VCO -> 2x oversample -> LadderVCF -> VCA
     *   -> Channel EQ (3-band biquad)
     *   -> Master Compressor (soft-knee, gentle ratio)
     *   -> Master EQ (lo/hi shelf)
     *   -> Stereo Width (M-S)
     *   -> BrickwallLimiter (lookahead, always on -- live AND render)
     *   -> Output (float32 stereo interleaved)
     *
     * @param outInterleaved Output buffer, interleaved L/R float32.
     * @param frameCount     Number of stereo frames to fill.
     */
    void processBlock(float* outInterleaved, int frameCount) {
        const float dt = static_cast<float>(frameCount) / sampleRate_;

        if (auto ev = sequencer.tick(dt)) {
            if (ev->noteOn) {
                // Apply arpeggiator to the pitch before triggering
                ev->pitch = arp.apply(ev->pitch);
                triggerVoice(*ev);
            } else {
                releaseAllVoices();
            }
        }

        const float masterGain = dsp::dbToLinear(master.gainDb);

        for (int i = 0; i < frameCount; ++i) {
            // Sum all active voices (mono)
            float mono = 0.f;
            for (auto& v : voices_)
                if (v.active) mono += v.processSample();

            // Soft-clip before EQ (gentle saturation)
            mono = dsp::softClip(mono * 0.7f);

            // Channel EQ
            if (channelEQ.enabled) {
                mono = eqLow_.process(mono);
                mono = eqMid_.process(mono);
                mono = eqHigh_.process(mono);
            }

            // Master gain
            float L = mono * masterGain;
            float R = mono * masterGain;

            // Master EQ
            L = masterLow_.process(L);  R = masterLow_.process(R);
            L = masterHigh_.process(L); R = masterHigh_.process(R);

            // Compressor (per channel, same settings)
            L = compressor_.process(L);
            R = compressor_.process(R);

            // Stereo width (M-S)
            stereoWidth_.process(L, R);

            // Store pre-limiter
            outInterleaved[i * 2    ] = L;
            outInterleaved[i * 2 + 1] = R;
        }

        // Brick-wall limiter -- ALWAYS active, prevents clipping in both live and render.
        limiter_.processBlock(outInterleaved, frameCount);
    }

    /// Sync EQ, compressor, and width from public param structs (call after UI changes).
    void syncParams() noexcept {
        eqLow_.setLowShelf(200.f,  channelEQ.lowGainDb);
        eqMid_.setPeakEQ(1000.f, 1.f, channelEQ.midGainDb);
        eqHigh_.setHighShelf(8000.f, channelEQ.highGainDb);

        compressor_.thresholdDb = master.compThresholdDb;
        compressor_.ratio       = master.compRatio;
        stereoWidth_.width      = master.stereoWidth;
        // Note: limiter threshold is set at init; rare to change live.
    }

    /// Open the default SDL3 audio output device and start the callback.
    bool startAudio() {
        if (stream_) return true;  // already running
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SDL_Log("[AudioEngine] SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
            return false;
        }
        SDL_AudioSpec spec;
        spec.format   = SDL_AUDIO_F32LE;
        spec.channels = 2;
        spec.freq     = kSampleRate;

        stream_ = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
            &spec,
            sdlAudioCallback,
            this);

        if (!stream_) {
            SDL_Log("[AudioEngine] SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
            return false;
        }
        SDL_ResumeAudioStreamDevice(stream_);
        SDL_Log("[AudioEngine] Audio started: %d Hz stereo f32", kSampleRate);
        return true;
    }

    void stopAudio() {
        if (stream_) {
            SDL_PauseAudioStreamDevice(stream_);
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
    }

    [[nodiscard]] bool isAudioRunning() const noexcept { return stream_ != nullptr; }

    /// Returns the current limiter gain reduction in dBFS (for a GR meter).
    [[nodiscard]] float limiterGainReductionDb() const noexcept {
        return limiter_.gainReductionDb();
    }

    /// Directly trigger a voice from an external StepEvent (for per-frame audio events).
    void triggerVoiceDirect(const StepEvent& ev) noexcept { triggerVoice(ev); }
    void releaseAllVoicesDirect()                noexcept { releaseAllVoices(); }

    /// Predefined C64/chip-tune voice configurations.
    enum class ChipPreset {
        Acid303,    ///< TB-303: saw + ladder filter + high resonance
        C64Lead,    ///< SID lead: 25% pulse + medium filter + sustain
        C64Bass,    ///< SID bass: triangle + high resonance + fast decay
        C64Arp,     ///< SID arp: saw + arpeggiator (major triad)
        NoiseDrum,  ///< LFSR noise + BP filter + instant decay (hi-hat/snare)
        GameBoy,    ///< DMG: 25% pulse, no filter, punchy ADSR
    };

    /**
     * @brief Apply a chip-tune voice preset to synth parameters.
     *
     * Overwrites vcfCutoffHz, vcfResonance, vcaSustainLvl, vcaDecaySecs and
     * configures the arpeggiator.  Call syncParams() afterwards if audio is live.
     *
     * @param preset  The desired sound character.
     */
    void applyChipPreset(ChipPreset preset) noexcept {
        arp.reset();
        arp.mode = Arpeggiator::Mode::Off;

        switch (preset) {
            case ChipPreset::Acid303:
                vcfCutoffHz   = 500.f;
                vcfResonance  = 0.75f;
                vcaSustainLvl = 0.f;
                vcaDecaySecs  = 0.3f;
                // Keep saw (waveShape 0)
                break;

            case ChipPreset::C64Lead:
                vcfCutoffHz   = 1800.f;
                vcfResonance  = 0.4f;
                vcaSustainLvl = 0.6f;
                vcaDecaySecs  = 0.5f;
                // 25% pulse — callers should set step waveShape=1, pw=0.25
                break;

            case ChipPreset::C64Bass:
                vcfCutoffHz   = 350.f;
                vcfResonance  = 0.65f;
                vcaSustainLvl = 0.f;
                vcaDecaySecs  = 0.4f;
                // Triangle — callers should set step waveShape=2
                break;

            case ChipPreset::C64Arp:
                vcfCutoffHz   = 2000.f;
                vcfResonance  = 0.3f;
                vcaSustainLvl = 0.2f;
                vcaDecaySecs  = 0.15f;
                arp.mode = Arpeggiator::Mode::Up;
                arp.setChord({0.f, 4.f, 7.f});   // major triad
                arp.octaves = 2;
                break;

            case ChipPreset::NoiseDrum:
                vcfCutoffHz   = 800.f;
                vcfResonance  = 0.f;
                vcaSustainLvl = 0.f;
                vcaDecaySecs  = 0.05f;
                // Noise — callers should set step waveShape=3
                break;

            case ChipPreset::GameBoy:
                vcfCutoffHz   = 8000.f;
                vcfResonance  = 0.f;
                vcaSustainLvl = 0.3f;
                vcaDecaySecs  = 0.2f;
                // 25% pulse
                break;
        }
    }

private:
    float sampleRate_ = static_cast<float>(kSampleRate);

    std::array<Voice303, kNumVoices> voices_{};
    dsp::BiquadFilter eqLow_, eqMid_, eqHigh_;
    dsp::BiquadFilter masterLow_, masterHigh_;
    dsp::Compressor   compressor_;
    dsp::StereoWidth  stereoWidth_;
    dsp::BrickwallLimiter limiter_;

    SDL_AudioStream* stream_ = nullptr;
    std::vector<float> mixBuf_;

    // WavRenderer needs direct limiter_.reset() access for clean render start
    friend class WavRenderer;

    /// Prefer idle voice; steal voices[0] as last resort.
    Voice303* allocVoice() noexcept {
        // Prefer idle voice
        for (auto& v : voices_)
            if (!v.active || v.isIdle()) return &v;
        // Steal the voice with the quietest envelope (simplest heuristic)
        return &voices_[0];
    }

    void triggerVoice(const StepEvent& ev) noexcept {
        Voice303* v = allocVoice();
        v->noteOn(ev, sampleRate_, vcfCutoffHz, vcfResonance,
                  vcaSustainLvl, vcaDecaySecs,
                  sequencer.accentParams);
        // Apply wave shape from step event
        v->waveShape_ = ev.waveShape;
        if (ev.waveShape == 1) {  // Square
            v->osc.shape = dsp::PolyBlepOscillator::Shape::Square;
            v->osc.pw    = ev.pulseWidth;
        } else if (ev.waveShape == 2) {  // Triangle
            v->osc.shape = dsp::PolyBlepOscillator::Shape::Triangle;
        } else {  // Saw or Noise both use osc.process() or noise_.process()
            v->osc.shape = dsp::PolyBlepOscillator::Shape::Saw;
        }
    }

    void releaseAllVoices() noexcept {
        for (auto& v : voices_) v.noteOff();
    }

    /// SDL3 audio callback (runs on the audio thread).
    static void sdlAudioCallback(void* userdata,
                                  SDL_AudioStream* /*stream*/,
                                  int additional_amount,
                                  int /*total_amount*/)
    {
        auto* eng  = static_cast<AudioEngine*>(userdata);
        int frames = additional_amount / static_cast<int>(sizeof(float) * 2);

        // Process in chunks of kBlockSize
        std::vector<float> buf(static_cast<std::size_t>(frames * 2));
        int filled = 0;
        while (filled < frames) {
            const int chunk = std::min(frames - filled, kBlockSize);
            eng->processBlock(buf.data() + filled * 2, chunk);
            filled += chunk;
        }
        SDL_PutAudioStreamData(eng->stream_,
                               buf.data(),
                               frames * static_cast<int>(sizeof(float)) * 2);
    }
};

} // namespace pelpaint::audio
