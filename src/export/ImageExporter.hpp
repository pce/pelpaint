#pragma once

#include "../ColorPalettes.hpp"
#include "../core/Error.hpp"
#include "DepthMapGenerator.hpp"
#include "ExportUtils.hpp"
#include "stb/stb_image_write.h"
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <array>
#include <span>
#include <cstring>

namespace pelpaint::exporter {

/// Lightweight per-layer metadata for PNG tEXt embedding.
/// Does not carry pixel data — use pelpaint::Layer for that.
struct LayerExportMeta {
    std::string_view name;
    int              zIndex  = 0;
    float            opacity = 1.0f;
    bool             visible = true;
};

/// Metadata extracted from a PNG file's pelpaint:meta tEXt chunk.
struct PngMetadata {
    struct LayerInfo {
        std::string name;
        int         zIndex  = 0;
        float       opacity = 1.0f;
        bool        visible = true;
    };

    std::uint32_t          width      = 0;     ///< from "w" field
    std::uint32_t          height     = 0;     ///< from "h" field
    std::string            appVersion;          ///< from "appVersion" field
    std::vector<LayerInfo> layers;              ///< from "layers" array
    std::string            rawJson;             ///< full tEXt value (forward compat)
    bool                   hasMeta    = false;  ///< false if no pelpaint:meta chunk
};

class ImageExporter {
private:
    /**
     * Efficiently read a pelpaint::Pixel from an ImageView at a specific coordinate.
     */
    static inline pelpaint::Pixel ReadPixel(const pelpaint::ImageView& view, std::uint32_t x, std::uint32_t y) noexcept {
        const std::uint8_t* p = view.data + (y * view.stride) + (x * view.channels);
        return pelpaint::Pixel(p[0], p[1], p[2], p[3]);
    }

public:
    /**
     * Optimized SVG Export using a Greedy Rectangle Merging algorithm.
     * Uses ImageView for efficient, non-owning access to image data.
     * Optimized with std::uint8_t visited buffer for faster access in hot loops.
     */
    static bool SaveToSVGOptimized(const std::string& filename, const pelpaint::ImageView& view) {
        if (!view.valid() || view.channels != 4) return false;

        std::ofstream file(filename, std::ios::out | std::ios::trunc);
        if (!file) return false;

        file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        file << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\"0 0 "
             << view.width << " " << view.height << "\" shape-rendering=\"crispEdges\">\n";

        // Using uint8_t instead of vector<bool> for better performance in tight loops
        std::vector<std::uint8_t> visited(view.width * view.height, 0);

        for (std::uint32_t y = 0; y < view.height; ++y) {
            for (std::uint32_t x = 0; x < view.width; ++x) {
                std::uint32_t idx = y * view.width + x;

                if (visited[idx]) continue;

                pelpaint::Pixel p = ReadPixel(view, x, y);

                // Skip transparent pixels
                if (p.a == 0) {
                    visited[idx] = 1;
                    continue;
                }

                // Greedy Merge Width:
                std::uint32_t rectW = 1;
                while (x + rectW < view.width) {
                    std::uint32_t nextIdx = y * view.width + (x + rectW);
                    if (visited[nextIdx]) break;

                    if (ReadPixel(view, x + rectW, y) == p) {
                        rectW++;
                    } else {
                        break;
                    }
                }

                // Greedy Merge Height:
                std::uint32_t rectH = 1;
                while (y + rectH < view.height) {
                    bool rowMatches = true;
                    for (std::uint32_t k = 0; k < rectW; ++k) {
                        std::uint32_t checkIdx = (y + rectH) * view.width + (x + k);
                        if (visited[checkIdx] || !(ReadPixel(view, x + k, y + rectH) == p)) {
                            rowMatches = false;
                            break;
                        }
                    }
                    if (rowMatches) {
                        rectH++;
                    } else {
                        break;
                    }
                }

                // Mark visited area
                for (std::uint32_t j = 0; j < rectH; ++j) {
                    for (std::uint32_t i = 0; i < rectW; ++i) {
                        visited[(y + j) * view.width + (x + i)] = 1;
                    }
                }

                file << "<rect x=\"" << x << "\" y=\"" << y
                     << "\" width=\"" << rectW << "\" height=\"" << rectH
                     << "\" fill=\"rgb(" << (int)p.r << "," << (int)p.g << "," << (int)p.b << ")\"";

                if (p.a < 255) {
                    file << " fill-opacity=\"" << std::fixed << std::setprecision(3) << (p.a / 255.0f) << "\"";
                }
                file << "/>\n";
            }
        }

        file << "</svg>\n";
        file.close();
        return !file.fail();
    }

    /**
     * Vector-style SVG using optimized merging and smoothed styling.
     * Includes slight rounding and overlap to prevent rendering artifacts.
     */
    static bool SaveToSVGVector(const std::string& filename, const pelpaint::ImageView& view) {
        if (!view.valid() || view.channels != 4) return false;

        std::ofstream file(filename, std::ios::out | std::ios::trunc);
        if (!file) return false;

        file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        file << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\"0 0 "
             << view.width << " " << view.height << "\">\n";

        std::vector<std::uint8_t> visited(view.width * view.height, 0);

        for (std::uint32_t y = 0; y < view.height; ++y) {
            for (std::uint32_t x = 0; x < view.width; ++x) {
                std::uint32_t idx = y * view.width + x;

                if (visited[idx]) continue;

                pelpaint::Pixel p = ReadPixel(view, x, y);
                if (p.a == 0) { visited[idx] = 1; continue; }

                std::uint32_t rectW = 1;
                while (x + rectW < view.width) {
                    if (visited[y * view.width + (x + rectW)]) break;
                    if (ReadPixel(view, x + rectW, y) == p) rectW++; else break;
                }

                std::uint32_t rectH = 1;
                while (y + rectH < view.height) {
                    bool rowMatches = true;
                    for (std::uint32_t k = 0; k < rectW; ++k) {
                        if (visited[(y + rectH) * view.width + (x + k)] || !(ReadPixel(view, x + k, y + rectH) == p)) {
                            rowMatches = false; break;
                        }
                    }
                    if (rowMatches) rectH++; else break;
                }

                for (std::uint32_t j = 0; j < rectH; ++j) {
                    for (std::uint32_t i = 0; i < rectW; ++i) visited[(y + j) * view.width + (x + i)] = 1;
                }

                file << "<rect x=\"" << x << "\" y=\"" << y
                     << "\" width=\"" << rectW + 0.05 << "\" height=\"" << rectH + 0.05
                     << "\" rx=\"0.4\" ry=\"0.4\" fill=\"rgb(" << (int)p.r << "," << (int)p.g << "," << (int)p.b << ")\"";

                if (p.a < 255) {
                    file << " fill-opacity=\"" << std::fixed << std::setprecision(3) << (p.a / 255.0f) << "\"";
                }
                file << "/>\n";
            }
        }

        file << "</svg>\n";
        file.close();
        return !file.fail();
    }

    static bool SaveToPNG(const std::string& filename, const pelpaint::ImageView& view) {
        if (!view.valid() || view.channels < 4) return false;

        return stbi_write_png(
            filename.c_str(),
            static_cast<int>(view.width),
            static_cast<int>(view.height),
            4,
            view.data,
            static_cast<int>(view.stride)
        ) != 0;
    }

    static bool SaveToTGA(const std::string& filename, const pelpaint::ImageView& view) {
        if (!view.valid() || view.channels < 4) return false;

        return stbi_write_tga(
            filename.c_str(),
            static_cast<int>(view.width),
            static_cast<int>(view.height),
            4,
            view.data
        ) != 0;
    }

    static bool SaveDepthMap(const pelpaint::ImageView& view,
                             std::uint32_t gridSize,
                             const std::string& filename)
    {
        if (!view.valid() || gridSize == 0) return false;

        std::vector<float> depthMap;
        if (!DepthMapGenerator::BuildDepthMap(view, gridSize, depthMap)) {
            return false;
        }

        const std::size_t sampleW = SampleWidth(view.width, gridSize);
        const std::size_t sampleH = SampleHeight(view.height, gridSize);

        if (depthMap.size() != sampleW * sampleH) return false;

        std::vector<std::uint8_t> gray;
        try {
            gray.resize(depthMap.size());
        } catch (...) {
            return false;
        }

        for (std::size_t i = 0; i < depthMap.size(); ++i) {
            const float d = Clamp01(depthMap[i]);
            const int v = static_cast<int>(d * 255.0f + 0.5f);
            gray[i] = static_cast<std::uint8_t>(std::min(std::max(v, 0), 255));
        }

        return stbi_write_png(
            filename.c_str(),
            static_cast<int>(sampleW),
            static_cast<int>(sampleH),
            1,
            gray.data(),
            static_cast<int>(sampleW)
        ) != 0;
    }

    // ---- std::expected-based overloads -----------------------------------

    /// Save PNG; returns Error on failure so callers can chain with .or_else().
    [[nodiscard]]
    static std::expected<void, pelpaint::Error>
    SaveToPNGExpected(const std::string& filename,
                      const pelpaint::ImageView& view)
    {
        if (!SaveToPNG(filename, view))
            return std::unexpected(pelpaint::Error::FileIO("PNG write failed"));
        return {};
    }

    /// Save TGA; returns Error on failure.
    [[nodiscard]]
    static std::expected<void, pelpaint::Error>
    SaveToTGAExpected(const std::string& filename,
                      const pelpaint::ImageView& view)
    {
        if (!SaveToTGA(filename, view))
            return std::unexpected(pelpaint::Error::FileIO("TGA write failed"));
        return {};
    }

    /// Save depth-map PNG; returns Error on failure.
    [[nodiscard]]
    static std::expected<void, pelpaint::Error>
    SaveDepthMapExpected(const pelpaint::ImageView& view,
                         std::uint32_t              gridSize,
                         const std::string&         filename)
    {
        if (!SaveDepthMap(view, gridSize, filename))
            return std::unexpected(pelpaint::Error::FileIO("Depth map save failed"));
        return {};
    }

    /// Save pixel-art SVG (optimized greedy-rect merging); returns Error on failure.
    [[nodiscard]]
    static std::expected<void, pelpaint::Error>
    SaveToSVGOptimizedExpected(const std::string& filename,
                               const pelpaint::ImageView& view)
    {
        if (!SaveToSVGOptimized(filename, view))
            return std::unexpected(pelpaint::Error::FileIO("SVG pixel save failed"));
        return {};
    }

    /// Save vector-styled SVG (rounded rects); returns Error on failure.
    [[nodiscard]]
    static std::expected<void, pelpaint::Error>
    SaveToSVGVectorExpected(const std::string& filename,
                            const pelpaint::ImageView& view)
    {
        if (!SaveToSVGVector(filename, view))
            return std::unexpected(pelpaint::Error::FileIO("SVG vector save failed"));
        return {};
    }

    /// Save a PNG with a pelpaint:meta tEXt chunk containing layer metadata.
    /// Injects the chunk after IHDR and before the first IDAT.
    /// Uses stbi_write_png_to_func to encode to memory, then patches the stream.
    [[nodiscard]]
    static std::expected<void, pelpaint::Error>
    SaveToPNGWithMeta(const std::string&                filename,
                      const pelpaint::ImageView&         view,
                      std::span<const LayerExportMeta>   layers,
                      std::string_view                   appVersion = "1.0")
    {
        if (!view.valid())
            return std::unexpected(pelpaint::Error::InvalidFormat());

        // Step 1: encode to in-memory PNG via stbi callback
        std::vector<std::uint8_t> pngBuf;
        auto writeFunc = +[](void* ctx, void* data, int size) {
            auto* buf = static_cast<std::vector<std::uint8_t>*>(ctx);
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            buf->insert(buf->end(), bytes, bytes + size);
        };
        if (!stbi_write_png_to_func(writeFunc, &pngBuf,
                static_cast<int>(view.width),
                static_cast<int>(view.height),
                static_cast<int>(view.channels),
                view.data,
                static_cast<int>(view.stride)))
            return std::unexpected(pelpaint::Error::FileIO("PNG encode failed"));

        // Step 2: find the first IDAT chunk offset
        // PNG signature is 8 bytes; each chunk: [4B len][4B type][N bytes][4B CRC]
        constexpr std::size_t kSigLen = 8;
        std::size_t pos = kSigLen;
        std::size_t idatPos = pngBuf.size(); // default: append before IEND if not found
        while (pos + 12 <= pngBuf.size()) {
            const std::uint32_t chunkLen =
                (static_cast<std::uint32_t>(pngBuf[pos    ]) << 24) |
                (static_cast<std::uint32_t>(pngBuf[pos + 1]) << 16) |
                (static_cast<std::uint32_t>(pngBuf[pos + 2]) <<  8) |
                (static_cast<std::uint32_t>(pngBuf[pos + 3]));
            // Check type bytes for "IDAT"
            if (pngBuf[pos+4]=='I' && pngBuf[pos+5]=='D' &&
                pngBuf[pos+6]=='A' && pngBuf[pos+7]=='T') {
                idatPos = pos;
                break;
            }
            pos += 12 + chunkLen; // 4 (len) + 4 (type) + chunkLen + 4 (crc)
        }

        // Step 3: build tEXt chunk
        const std::string meta = BuildMetaJson(view, layers, appVersion);
        const auto textChunk   = BuildTextChunk("pelpaint:meta", meta);

        // Step 4: splice the tEXt chunk in before IDAT
        std::vector<std::uint8_t> result;
        result.reserve(pngBuf.size() + textChunk.size());
        result.insert(result.end(), pngBuf.begin(), pngBuf.begin() + static_cast<std::ptrdiff_t>(idatPos));
        result.insert(result.end(), textChunk.begin(), textChunk.end());
        result.insert(result.end(), pngBuf.begin() + static_cast<std::ptrdiff_t>(idatPos), pngBuf.end());

        // Step 5: write to disk
        std::ofstream f(filename, std::ios::binary | std::ios::trunc);
        if (!f) return std::unexpected(pelpaint::Error::FileIO("Cannot open file for writing"));
        f.write(reinterpret_cast<const char*>(result.data()),
                static_cast<std::streamsize>(result.size()));
        if (f.fail()) return std::unexpected(pelpaint::Error::FileIO("PNG write failed"));
        return {};
    }

    /// Extract pelpaint:meta from a PNG file on disk.
    /// Returns PngMetadata with hasMeta=false if the file is valid PNG
    /// but contains no pelpaint:meta tEXt chunk.
    [[nodiscard]]
    static std::expected<PngMetadata, pelpaint::Error>
    ReadPNGMeta(const std::string& filename)
    {
        std::ifstream f(filename, std::ios::binary | std::ios::ate);
        if (!f) return std::unexpected(pelpaint::Error::FileIO("Cannot open PNG"));
        const auto fileSize = static_cast<std::size_t>(f.tellg());
        f.seekg(0);
        std::vector<std::uint8_t> buf(fileSize);
        f.read(reinterpret_cast<char*>(buf.data()),
               static_cast<std::streamsize>(fileSize));
        if (f.fail()) return std::unexpected(pelpaint::Error::FileIO("Read failed"));
        return ReadPNGMetaFromMemory(std::span<const std::uint8_t>{buf});
    }

    /// Extract pelpaint:meta from an in-memory PNG blob.
    /// Useful when the blob was decoded from a project file without touching disk.
    [[nodiscard]]
    static std::expected<PngMetadata, pelpaint::Error>
    ReadPNGMetaFromMemory(std::span<const std::uint8_t> pngBlob)
    {
        constexpr std::array<std::uint8_t, 8> kPngSig{
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
        if (pngBlob.size() < 8 ||
            !std::equal(kPngSig.begin(), kPngSig.end(), pngBlob.begin()))
            return std::unexpected(pelpaint::Error::InvalidFormat());

        std::string found = FindPelPaintMetaChunk(pngBlob);
        if (found.empty()) {
            PngMetadata empty;
            empty.hasMeta = false;
            return empty;
        }
        return ParseMetaJson(std::move(found));
    }

private:
    // ---- PNG binary helpers (used by SaveToPNGWithMeta) -----------------

    static std::uint32_t Crc32(const std::uint8_t* data, std::size_t len) noexcept {
        // Build lookup table on first call (Meyer's singleton pattern).
        static const auto table = []() noexcept {
            std::array<std::uint32_t, 256> t{};
            for (std::uint32_t n = 0; n < 256; ++n) {
                std::uint32_t c = n;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
                t[n] = c;
            }
            return t;
        }();
        std::uint32_t c = 0xFFFFFFFFUL;
        for (std::size_t n = 0; n < len; ++n)
            c = table[(c ^ data[n]) & 0xFFu] ^ (c >> 8);
        return c ^ 0xFFFFFFFFUL;
    }

    static void WriteBE32(std::vector<std::uint8_t>& out, std::uint32_t v) {
        out.push_back(static_cast<std::uint8_t>(v >> 24));
        out.push_back(static_cast<std::uint8_t>(v >> 16));
        out.push_back(static_cast<std::uint8_t>(v >>  8));
        out.push_back(static_cast<std::uint8_t>(v));
    }

    static std::vector<std::uint8_t>
    BuildTextChunk(std::string_view keyword, std::string_view text) {
        // chunk = [4B length][4B type "tEXt"][keyword \0 text][4B CRC32]
        std::vector<std::uint8_t> data;
        data.insert(data.end(), keyword.begin(), keyword.end());
        data.push_back(0u);   // null separator
        data.insert(data.end(), text.begin(), text.end());

        constexpr std::array<std::uint8_t, 4> kType{'t','E','X','t'};
        // CRC covers type + data
        std::vector<std::uint8_t> crcInput(kType.begin(), kType.end());
        crcInput.insert(crcInput.end(), data.begin(), data.end());
        const std::uint32_t crc = Crc32(crcInput.data(), crcInput.size());

        std::vector<std::uint8_t> chunk;
        WriteBE32(chunk, static_cast<std::uint32_t>(data.size()));
        chunk.insert(chunk.end(), kType.begin(), kType.end());
        chunk.insert(chunk.end(), data.begin(), data.end());
        WriteBE32(chunk, crc);
        return chunk;
    }

    static std::string BuildMetaJson(
        const pelpaint::ImageView&       view,
        std::span<const LayerExportMeta> layers,
        std::string_view                 appVersion)
    {
        std::string j;
        j += "{\"version\":1";
        j += ",\"app\":\"pelpaint\"";
        j += ",\"appVersion\":\""; j += appVersion; j += "\"";
        j += ",\"w\":";  j += std::to_string(view.width);
        j += ",\"h\":";  j += std::to_string(view.height);
        j += ",\"layers\":[";
        for (std::size_t i = 0; i < layers.size(); ++i) {
            if (i > 0) j += ',';
            const auto& l = layers[i];
            j += "{\"name\":\"";
            // Escape backslash and double-quote in the name
            for (char c : l.name) {
                if (c == '"' || c == '\\') j += '\\';
                j += c;
            }
            j += "\"";
            j += ",\"z\":";  j += std::to_string(l.zIndex);
            j += ",\"op\":"; j += std::to_string(l.opacity);
            j += ",\"vis\":"; j += l.visible ? "true" : "false";
            j += "}";
        }
        j += "]}";
        return j;
    }


    /// Walk the chunk stream and return the tEXt value for "pelpaint:meta", or "".
    static std::string FindPelPaintMetaChunk(std::span<const std::uint8_t> png)
    {
        constexpr std::size_t kSigLen = 8;
        std::size_t pos = kSigLen;
        while (pos + 12 <= png.size()) {
            const std::uint32_t chunkLen =
                (static_cast<std::uint32_t>(png[pos    ]) << 24) |
                (static_cast<std::uint32_t>(png[pos + 1]) << 16) |
                (static_cast<std::uint32_t>(png[pos + 2]) <<  8) |
                (static_cast<std::uint32_t>(png[pos + 3]));
            const std::size_t dataStart = pos + 8;
            const std::size_t chunkEnd  = dataStart + chunkLen + 4;
            if (chunkEnd > png.size()) break;

            if (png[pos+4]=='t' && png[pos+5]=='E' &&
                png[pos+6]=='X' && png[pos+7]=='t') {
                std::size_t sep = dataStart;
                while (sep < dataStart + chunkLen && png[sep] != 0) ++sep;
                if (sep < dataStart + chunkLen) {
                    const std::string_view keyword{
                        reinterpret_cast<const char*>(png.data() + dataStart),
                        sep - dataStart};
                    if (keyword == "pelpaint:meta") {
                        const std::size_t textStart = sep + 1;
                        const std::size_t textLen   = dataStart + chunkLen - textStart;
                        return std::string{
                            reinterpret_cast<const char*>(png.data() + textStart),
                            textLen};
                    }
                }
            }
            if (png[pos+4]=='I' && png[pos+5]=='E' &&
                png[pos+6]=='N' && png[pos+7]=='D') break;
            pos = chunkEnd;
        }
        return {};
    }

    static std::string ExtractJsonString(std::string_view json, std::string_view key) {
        std::string needle;
        needle.reserve(key.size() + 4);
        needle += '"'; needle += key; needle += "\":\"";
        const auto pos0 = json.find(needle);
        if (pos0 == std::string_view::npos) return {};
        std::size_t pos = pos0 + needle.size();
        std::string out;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) { ++pos; out += json[pos]; }
            else                                               out += json[pos];
            ++pos;
        }
        return out;
    }

    static std::uint32_t ExtractJsonUInt(std::string_view json, std::string_view key) {
        std::string needle;
        needle += '"'; needle += key; needle += "\":";
        const auto pos0 = json.find(needle);
        if (pos0 == std::string_view::npos) return 0u;
        std::size_t pos = pos0 + needle.size();
        while (pos < json.size() && json[pos] == ' ') ++pos;
        std::uint32_t v = 0;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
            v = v * 10u + static_cast<std::uint32_t>(json[pos++] - '0');
        return v;
    }

    static int ExtractJsonInt(std::string_view json, std::string_view key) {
        std::string needle;
        needle += '"'; needle += key; needle += "\":";
        const auto pos0 = json.find(needle);
        if (pos0 == std::string_view::npos) return 0;
        std::size_t pos = pos0 + needle.size();
        while (pos < json.size() && json[pos] == ' ') ++pos;
        const bool neg = (pos < json.size() && json[pos] == '-');
        if (neg) ++pos;
        int v = 0;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
            v = v * 10 + (json[pos++] - '0');
        return neg ? -v : v;
    }

    static float ExtractJsonFloat(std::string_view json, std::string_view key) {
        std::string needle;
        needle += '"'; needle += key; needle += "\":";
        const auto pos0 = json.find(needle);
        if (pos0 == std::string_view::npos) return 0.f;
        std::size_t pos = pos0 + needle.size();
        while (pos < json.size() && json[pos] == ' ') ++pos;
        const char* start = json.data() + pos;
        char* end = nullptr;
        const float v = std::strtof(start, &end);
        return (end > start) ? v : 0.f;
    }

    static bool ExtractJsonBool(std::string_view json, std::string_view key,
                                 bool dflt = true) {
        std::string needle;
        needle += '"'; needle += key; needle += "\":";
        const auto pos0 = json.find(needle);
        if (pos0 == std::string_view::npos) return dflt;
        std::size_t pos = pos0 + needle.size();
        while (pos < json.size() && json[pos] == ' ') ++pos;
        return json.size() - pos >= 4 &&
               json[pos]=='t' && json[pos+1]=='r' && json[pos+2]=='u' && json[pos+3]=='e';
    }

    static std::vector<PngMetadata::LayerInfo>
    ExtractJsonLayers(std::string_view json) {
        std::vector<PngMetadata::LayerInfo> result;
        constexpr std::string_view kNeedle = "\"layers\":[";
        const auto arrPos = json.find(kNeedle);
        if (arrPos == std::string_view::npos) return result;

        std::size_t pos = arrPos + kNeedle.size();
        int depth = 1;
        const std::size_t contentStart = pos;
        while (pos < json.size() && depth > 0) {
            if      (json[pos] == '[') ++depth;
            else if (json[pos] == ']') --depth;
            ++pos;
        }
        if (depth != 0) return result;
        const auto content = json.substr(contentStart, pos - contentStart - 1);

        std::size_t cur = 0;
        while (cur < content.size()) {
            while (cur < content.size() && content[cur] != '{') ++cur;
            if (cur >= content.size()) break;
            const std::size_t objStart = cur++;
            int objDepth = 1;
            while (cur < content.size() && objDepth > 0) {
                if      (content[cur] == '{') ++objDepth;
                else if (content[cur] == '}') --objDepth;
                ++cur;
            }
            if (objDepth != 0) break;
            const auto obj = content.substr(objStart, cur - objStart);
            PngMetadata::LayerInfo li;
            li.name    = ExtractJsonString(obj, "name");
            li.zIndex  = ExtractJsonInt(obj, "z");
            li.opacity = ExtractJsonFloat(obj, "op");
            li.visible = ExtractJsonBool(obj, "vis");
            result.push_back(std::move(li));
        }
        return result;
    }

    static PngMetadata ParseMetaJson(std::string raw) {
        PngMetadata meta;
        meta.hasMeta    = true;
        const std::string_view json{raw};
        meta.width      = ExtractJsonUInt(json, "w");
        meta.height     = ExtractJsonUInt(json, "h");
        meta.appVersion = ExtractJsonString(json, "appVersion");
        meta.layers     = ExtractJsonLayers(json);
        meta.rawJson    = std::move(raw);
        return meta;
    }
};
} // namespace pelpaint::exporter
