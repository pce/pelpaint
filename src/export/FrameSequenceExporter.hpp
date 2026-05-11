#pragma once

/**
 * @file FrameSequenceExporter.hpp
 * @brief Export an AnimationTimeline as a zero-padded image sequence.
 *
 * Frames are written as  <basename>_NNN.<ext>  where NNN is zero-padded
 * to at least 3 digits (more if frameCount > 999), ensuring natural sort
 * order in every file manager and shell glob.
 *
 * Example for 12 frames: frame_001.png … frame_012.png
 */

#include "../core/AnimationTimeline.hpp"
#include "../core/ImageSurface.hpp"
#include "../core/Types.hpp"
#include "ImageExporter.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace pelpaint::exporter {

class FrameSequenceExporter {
public:
    struct Result {
        bool        ok        = false;
        int         written   = 0;    ///< frames successfully written
        int         total     = 0;    ///< total frames attempted
        std::string firstPath;        ///< path of the first written frame (UI feedback)
        std::string error;
    };

    /// Zero-padded frame index suffix, e.g. "_001" for idx=0 when total=500.
    ///
    /// Width = max(3, digits needed for (total-1)), so:
    ///   1..999 frames   → "_001"  (width 3)
    ///   1000..9999      → "_0001" (width 4)
    [[nodiscard]]
    static std::string FrameSuffix(int idx, int total)
    {
        const int width = std::max(3, static_cast<int>(
            std::to_string(std::max(1, total - 1)).size()));
        char buf[16];
        std::snprintf(buf, sizeof(buf), "_%0*d", width, idx);
        return std::string(buf);
    }

    /// The zero-padding width used for a given total frame count.
    [[nodiscard]]
    static int PadWidth(int total) noexcept
    {
        return std::max(3, static_cast<int>(
            std::to_string(std::max(1, total - 1)).size()));
    }

    /// Full output path for one frame.
    [[nodiscard]]
    static std::string FramePath(const std::string& dir,
                                  const std::string& basename,
                                  int                idx,
                                  int                total,
                                  std::string_view   ext = "png")
    {
        namespace fs = std::filesystem;
        return (fs::path(dir) /
                (basename + FrameSuffix(idx, total) + '.' + std::string(ext)))
               .string();
    }

    /// Export every frame of @p timeline as individual image files.
    ///
    /// @param timeline   Source animation. Each frame's surface is Flatten()ed.
    /// @param outputDir  Destination directory — created if it does not exist.
    /// @param basename   Filename stem (no extension), e.g. "frame".
    /// @param ext        Image format: "png" (default) or "tga".
    ///
    /// @note Flatten() reuses a scratch buffer per ImageSurface instance, so
    ///       writing each frame immediately after Flatten() is required (done here).
    [[nodiscard]]
    static Result Export(const core::AnimationTimeline& timeline,
                         const std::string&             outputDir,
                         const std::string&             basename,
                         std::string_view               ext = "png")
    {
        namespace fs = std::filesystem;
        Result res;
        res.total = timeline.FrameCount();

        if (res.total == 0) {
            res.error = "No frames to export";
            return res;
        }

        std::error_code ec;
        fs::create_directories(outputDir, ec);
        if (ec) {
            res.error = "Cannot create output directory: " + ec.message();
            return res;
        }

        for (int i = 0; i < res.total; ++i) {
            const std::string path = FramePath(outputDir, basename, i, res.total, ext);
            if (i == 0) res.firstPath = path;

            const core::AnimationFrame& af   = timeline.Frame(i);
            const core::ImageView       flat = af.surface.Flatten();
            if (!flat.valid()) continue;

            // Bridge core::ImageView -> pelpaint::ImageView for ImageExporter.
            pelpaint::ImageView view;
            view.data     = flat.data;
            view.width    = flat.width;
            view.height   = flat.height;
            view.stride   = flat.stride;
            view.channels = 4;

            const bool ok = (ext == "tga")
                ? ImageExporter::SaveToTGA(path, view)
                : ImageExporter::SaveToPNG(path, view);

            if (ok) ++res.written;
        }

        res.ok = (res.written == res.total);
        if (!res.ok)
            res.error = std::to_string(res.total - res.written)
                      + " frame(s) failed to write";
        return res;
    }
};

} // namespace pelpaint::exporter
