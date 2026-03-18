#pragma once

#include "../ColorPalettes.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <iomanip>

class ImageExporter {
public:
    /**
     * Optimized SVG Export using a Greedy Rectangle Merging algorithm.
     * This reduces the number of SVG elements from millions (one per pixel)
     * to thousands by merging adjacent pixels of the same color into larger <rect> blocks.
     */
    static bool SaveToSVGOptimized(const std::string& filename, int width, int height, const std::vector<Pixel>& data) {
        if (data.size() != static_cast<size_t>(width * height)) return false;

        std::ofstream file(filename, std::ios::out | std::ios::trunc);
        if (!file) return false;

        file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        file << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\"0 0 "
             << width << " " << height << "\" shape-rendering=\"crispEdges\">\n";

        // Keep track of which pixels have been visited/merged
        std::vector<bool> visited(width * height, false);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = y * width + x;
                const Pixel& p = data[idx];

                // Skip transparent or already processed pixels
                if (p.a == 0 || visited[idx]) {
                    visited[idx] = true;
                    continue;
                }

                // Greedy Merge:
                // 1. Find max width of same-color run on current line
                int rectW = 1;
                while (x + rectW < width) {
                    int nextIdx = y * width + (x + rectW);
                    if (!visited[nextIdx] && data[nextIdx] == p) {
                        rectW++;
                    } else {
                        break;
                    }
                }

                // 2. See how many subsequent rows can accommodate this same width/color
                int rectH = 1;
                while (y + rectH < height) {
                    bool rowMatches = true;
                    for (int k = 0; k < rectW; ++k) {
                        int checkIdx = (y + rectH) * width + (x + k);
                        if (visited[checkIdx] || !(data[checkIdx] == p)) {
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

                // Mark all pixels in the merged rectangle as visited
                for (int j = 0; j < rectH; ++j) {
                    for (int i = 0; i < rectW; ++i) {
                        visited[(y + j) * width + (x + i)] = true;
                    }
                }

                // Output the merged rectangle
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
     * Vector-style SVG using path approximations or rounded rectangles.
     * This version uses the same optimized merging but adds styling for a "smoother" look.
     */
    static bool SaveToSVGVector(const std::string& filename, int width, int height, const std::vector<Pixel>& data) {
        if (data.size() != static_cast<size_t>(width * height)) return false;

        std::ofstream file(filename, std::ios::out | std::ios::trunc);
        if (!file) return false;

        file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        file << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\"0 0 "
             << width << " " << height << "\">\n";

        std::vector<bool> visited(width * height, false);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = y * width + x;
                const Pixel& p = data[idx];
                if (p.a == 0 || visited[idx]) { visited[idx] = true; continue; }

                int rectW = 1;
                while (x + rectW < width && !visited[y * width + (x + rectW)] && data[y * width + (x + rectW)] == p) {
                    rectW++;
                }

                int rectH = 1;
                while (y + rectH < height) {
                    bool rowMatches = true;
                    for (int k = 0; k < rectW; ++k) {
                        if (visited[(y + rectH) * width + (x + k)] || !(data[(y + rectH) * width + (x + k)] == p)) {
                            rowMatches = false; break;
                        }
                    }
                    if (rowMatches) rectH++; else break;
                }

                for (int j = 0; j < rectH; ++j) {
                    for (int i = 0; i < rectW; ++i) visited[(y + j) * width + (x + i)] = true;
                }

                // Add slight rounding (rx/ry) to merged blocks for a smoother "vector" look
                // and a tiny overlap (width+0.05) to prevent hairline gaps in some renderers.
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
};
