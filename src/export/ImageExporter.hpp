#pragma once

#include "../ColorPalettes.hpp"
#include "../PixelPaintView.hpp"
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

namespace pelpaint::exporter {

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
};
} // namespace pelpaint::exporter
