#pragma once

/**
 * @file WavRenderer.hpp
 * @brief Offline WAV render via AudioEngine::processBlock().
 *
 * Float32 to Int16 conversion (final clamp-and-scale) happens only at the
 * WAV write stage; the signal chain stays float32 throughout.
 *
 * Output format: 16-bit PCM stereo WAV, little-endian.
 */

#include "AudioEngine.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace pelpaint::audio {

/// @brief Configuration options for an offline WAV render.
struct WavRenderOptions {
    /// If 0, compute from sequencer: (loopCount × loop_length × step_duration)
    float durationSecs = 0.f;

    /// How many full sequencer loops to render (used when durationSecs==0).
    int   loopCount    = 2;

    /// Extra tail (seconds) for reverb / envelope decay.
    float tailSecs     = 1.5f;

    /// Sample rate (must match AudioEngine::kSampleRate for correct BPM).
    int   sampleRate   = AudioEngine::kSampleRate;
};

/**
 * @brief Renders an AudioEngine to a 16-bit stereo WAV file offline.
 *
 * @note Calls AudioEngine::processBlock() directly -- no effect bypass,
 *       no resampling. Render == live, always.
 */
class WavRenderer {
public:
    struct Result {
        bool        ok      = false;
        std::string error;
        int         frames  = 0;   ///< total stereo frames rendered
        float       peakDb  = 0.f; ///< peak signal level after limiter (dBFS)
        float       grDb    = 0.f; ///< final limiter gain reduction (dBFS)
    };

    /// Render the engine to a 16-bit stereo WAV file.
    ///
    /// The engine's SDL3 audio device must NOT be running — call
    /// engine.stopAudio() first (or use a separate engine instance for render).
    ///
    /// The engine's sequencer must be configured before calling.
    /// This function rewinds the sequencer, sets playing=true, renders,
    /// then restores the original playing state.
    static Result RenderToWav(AudioEngine&           engine,
                               const std::string&     outputPath,
                               const WavRenderOptions& opts = {})
    {
        // Sanity checks.
        if (engine.isAudioRunning()) {
            return { false, "Stop live audio before rendering (call engine.stopAudio())" };
        }

        const int sr = opts.sampleRate;
        if (sr <= 0) return { false, "Invalid sample rate" };

        // Compute render duration.
        float durSecs = opts.durationSecs;
        if (durSecs <= 0.f) {
            const float loopSecs = static_cast<float>(engine.sequencer.loopLength())
                                 * engine.sequencer.stepDuration();
            durSecs = loopSecs * static_cast<float>(std::max(1, opts.loopCount));
        }
        durSecs += opts.tailSecs;

        const int totalFrames = static_cast<int>(durSecs * static_cast<float>(sr));
        if (totalFrames <= 0)
            return { false, "Computed render duration is zero" };

        // Prepare engine.
        const bool wasPlaying = engine.sequencer.playing;
        engine.sequencer.rewind();
        engine.sequencer.playing = true;
        engine.limiter_.reset();    // clear lookahead buffer for clean start

        // Allocate float32 stereo interleaved output buffer.
        std::vector<float> floatBuf(static_cast<std::size_t>(totalFrames * 2), 0.f);

        // Render in blocks.
        float peakLinear = 0.f;
        int   rendered   = 0;

        while (rendered < totalFrames) {
            const int chunk = std::min(totalFrames - rendered, AudioEngine::kBlockSize);
            engine.processBlock(floatBuf.data() + rendered * 2, chunk);

            // Track peak
            for (int i = 0; i < chunk * 2; ++i) {
                const float a = std::abs(floatBuf[rendered * 2 + i]);
                if (a > peakLinear) peakLinear = a;
            }
            rendered += chunk;
        }

        const float grDb = engine.limiterGainReductionDb();

        // Restore sequencer state
        engine.sequencer.playing = wasPlaying;

        // Convert float32 to int16.
        // Limiter already prevents > +/-1.0f; the clamp here is belt+suspenders.
        std::vector<std::int16_t> pcm(static_cast<std::size_t>(totalFrames * 2));
        for (int i = 0; i < totalFrames * 2; ++i) {
            const float clamped = std::clamp(floatBuf[i], -1.f, 1.f);
            pcm[i] = static_cast<std::int16_t>(clamped * 32767.f);
        }

        // Write WAV file.
        std::ofstream f(outputPath, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
            return { false, "Cannot open output file: " + outputPath };

        writeWavHeader(f, sr, 2, totalFrames);
        f.write(reinterpret_cast<const char*>(pcm.data()),
                static_cast<std::streamsize>(pcm.size() * sizeof(std::int16_t)));

        if (!f.good())
            return { false, "Write error: " + outputPath };

        return Result{
            true, {},
            totalFrames,
            dsp::linearToDb(peakLinear),
            grDb
        };
    }

private:
    /// Write a standard 44-byte RIFF WAV header for 16-bit PCM stereo.
    static void writeWavHeader(std::ofstream& f,
                                int            sampleRate,
                                int            numChannels,
                                int            numSamples) noexcept
    {
        const std::uint32_t dataBytes  = static_cast<std::uint32_t>(
                                             numSamples * numChannels * 2);
        const std::uint32_t chunkSize  = 36u + dataBytes;
        const std::uint32_t byteRate   = static_cast<std::uint32_t>(
                                             sampleRate * numChannels * 2);
        const std::uint16_t blockAlign = static_cast<std::uint16_t>(numChannels * 2);

        auto w4  = [&](const char* s){ f.write(s, 4); };
        auto wu32 = [&](std::uint32_t v){ f.write(reinterpret_cast<char*>(&v), 4); };
        auto wu16 = [&](std::uint16_t v){ f.write(reinterpret_cast<char*>(&v), 2); };

        w4("RIFF");
        wu32(chunkSize);
        w4("WAVE");

        w4("fmt ");
        wu32(16u);                                       // subchunk1 size (PCM)
        wu16(1u);                                        // AudioFormat: PCM
        wu16(static_cast<std::uint16_t>(numChannels));
        wu32(static_cast<std::uint32_t>(sampleRate));
        wu32(byteRate);
        wu16(blockAlign);
        wu16(16u);                                       // bits per sample

        w4("data");
        wu32(dataBytes);
    }

    // AudioEngine::limiter_ is private — grant access for reset
    friend class AudioEngine;
};

} // namespace pelpaint::audio
