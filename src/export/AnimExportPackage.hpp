#pragma once

/**
 * @file AnimExportPackage.hpp
 * @brief Bundle animation frames + audio into a self-contained export folder.
 *
 * Creates a timestamped subfolder inside the chosen root directory:
 *
 *   <root>/<stem>_YYYYMMDD_HHMMSS/
 *       frames/
 *           frame_001.png
 *           frame_002.png
 *           ...
 *       audio.wav
 *       info.json
 *       combine.sh      (Linux / macOS ffmpeg one-liner)
 *       combine.bat     (Windows ffmpeg one-liner)
 *
 * The audio WAV is rendered for exactly frameCount/fps seconds so it matches
 * the frame sequence when recombined.  The helper scripts carry the exact
 * ffmpeg invocation so the user does not need to remember the padding format.
 */

#include "FrameSequenceExporter.hpp"
#include "../audio/AudioEngine.hpp"
#include "../audio/WavRenderer.hpp"
#include "../core/AnimationTimeline.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace pelpaint::exporter {

struct AnimExportOptions {
    std::string_view imageExt  = "png";    ///< "png" or "tga"
    std::string_view frameStem = "frame";  ///< basename for frame files
};

struct AnimExportResult {
    bool        ok             = false;
    std::string packageDir;               ///< absolute path of the created folder
    int         framesWritten  = 0;
    int         framesTotal    = 0;
    bool        audioWritten   = false;
    float       audioPeakDb    = 0.f;
    std::string error;
};

class AnimExportPackage {
public:
    /**
     * @brief Create the export package: frames + audio + metadata + combine scripts.
     *
     * @param rootDir      Parent directory for the package folder.
     * @param projectStem  Used in the folder name (e.g. "sprite" → "sprite_20250101_120000/").
     * @param timeline     Animation source (read-only; at least 1 frame required).
     * @param engine       AudioEngine — must NOT be running (call stopAudio() first).
     * @param opts         Image format and frame stem options.
     *
     * @note The audio is rendered for exactly frameCount/fps seconds so the WAV
     *       matches the frame sequence duration.  If the sequencer pattern is shorter
     *       it loops; if longer it is truncated.
     */
    [[nodiscard]]
    static AnimExportResult Export(
        const std::string&             rootDir,
        const std::string&             projectStem,
        const core::AnimationTimeline& timeline,
        audio::AudioEngine&            engine,
        const AnimExportOptions&       opts = {})
    {
        namespace fs = std::filesystem;
        AnimExportResult result;

        const int   nFrames = timeline.FrameCount();
        const float fps     = timeline.FPS();

        if (nFrames == 0) {
            result.error = "No frames to export";
            return result;
        }

        // Create timestamped package subfolder
        const std::string folderName = projectStem + "_" + Timestamp();
        const fs::path    pkg        = fs::path(rootDir) / folderName;
        const fs::path    framesDir  = pkg / "frames";

        std::error_code ec;
        fs::create_directories(framesDir, ec);
        if (ec) {
            result.error = "Cannot create package directory: " + ec.message();
            return result;
        }
        result.packageDir = pkg.string();

        // 1. Export frame sequence
        const auto seqResult = FrameSequenceExporter::Export(
            timeline,
            framesDir.string(),
            std::string(opts.frameStem),
            opts.imageExt);

        result.framesWritten = seqResult.written;
        result.framesTotal   = seqResult.total;

        if (seqResult.written == 0) {
            result.error = "Frame export failed: " + seqResult.error;
            return result;
        }

        // 2. Export audio — duration exactly matches the frame sequence
        const float durSecs = (fps > 0.f && nFrames > 0)
                              ? static_cast<float>(nFrames) / fps
                              : 0.f;

        const std::string audioPath = (pkg / "audio.wav").string();
        audio::WavRenderOptions wavOpts;
        wavOpts.durationSecs = durSecs;
        wavOpts.tailSecs     = 0.f;     // no decay tail — tight frame sync

        const auto wavResult = audio::WavRenderer::RenderToWav(engine, audioPath, wavOpts);
        result.audioWritten  = wavResult.ok;
        result.audioPeakDb   = wavResult.peakDb;

        // 3. Write info.json
        writeInfoJson(pkg / "info.json", timeline, engine, wavResult);

        // 4. Write combine scripts
        const int pad = FrameSequenceExporter::PadWidth(nFrames);
        writeCombineScripts(pkg,
                            std::string(opts.frameStem),
                            std::string(opts.imageExt),
                            fps,
                            pad);

        result.ok = true;
        return result;
    }

private:
    static std::string Timestamp()
    {
        const auto now  = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto tm   = *std::localtime(&time);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
        return oss.str();
    }

    static void writeInfoJson(
        const std::filesystem::path&       path,
        const core::AnimationTimeline&     timeline,
        const audio::AudioEngine&          engine,
        const audio::WavRenderer::Result&  wavRes)
    {
        std::ofstream f(path);
        if (!f.is_open()) return;

        const float fps     = timeline.FPS();
        const int   nFrames = timeline.FrameCount();
        const float dur     = (fps > 0.f) ? static_cast<float>(nFrames) / fps : 0.f;

        f << "{\n"
          << "  \"frame_count\": "   << nFrames                           << ",\n"
          << "  \"fps\": "           << fps                               << ",\n"
          << "  \"duration_secs\": " << dur                               << ",\n"
          << "  \"bpm\": "           << engine.sequencer.bpm              << ",\n"
          << "  \"audio_ok\": "      << (wavRes.ok ? "true" : "false")    << ",\n"
          << "  \"audio_peak_db\": " << wavRes.peakDb                     << ",\n"
          << "  \"audio_gr_db\": "   << wavRes.grDb                       << "\n"
          << "}\n";
    }

    static void writeCombineScripts(
        const std::filesystem::path& pkg,
        const std::string&           stem,
        const std::string&           ext,
        float                        fps,
        int                          pad)
    {
        const int ifps = std::max(1, static_cast<int>(std::round(fps)));

        // Shell script (Linux / macOS)
        {
            std::ofstream f(pkg / "combine.sh");
            f << "#!/bin/sh\n"
              << "# Combine frames + audio into MP4 (requires ffmpeg)\n"
              << "ffmpeg -framerate " << ifps << " \\\n"
              << "       -i frames/" << stem << "_%0" << pad << "d." << ext << " \\\n"
              << "       -i audio.wav \\\n"
              << "       -c:v libx264 -pix_fmt yuv420p \\\n"
              << "       -c:a aac -b:a 192k -shortest \\\n"
              << "       output.mp4\n"
              << "echo \"Done: output.mp4\"\n";
        }

        // Batch file (Windows)
        // In .bat files % must be doubled: %03d -> %%03d
        {
            std::ofstream f(pkg / "combine.bat");
            f << "@echo off\n"
              << "REM Combine frames + audio into MP4 (requires ffmpeg)\n"
              << "ffmpeg -framerate " << ifps << " ^\n"
              << "       -i frames\\" << stem << "_%%0" << pad << "d." << ext << " ^\n"
              << "       -i audio.wav ^\n"
              << "       -c:v libx264 -pix_fmt yuv420p ^\n"
              << "       -c:a aac -b:a 192k -shortest ^\n"
              << "       output.mp4\n"
              << "echo Done: output.mp4\n";
        }
    }
};

} // namespace pelpaint::exporter
