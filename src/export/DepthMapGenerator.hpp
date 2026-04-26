#pragma once

#include "../ColorPalettes.hpp"
#include "../PixelPaintView.hpp"
#include "ExportUtils.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace pelpaint::exporter {
    class DepthMapGenerator {
    public:
    static bool BuildDepthMap(const pelpaint::ImageView& view,
                                     std::uint32_t gridSize,
                                     std::vector<float>& outDepthMap)
    {
        outDepthMap.clear();

        if (!view.valid() || view.data == nullptr || view.channels < 4) return false;
        if (gridSize == 0) return false;

        const std::size_t sampleW = SampleWidth(view.width, gridSize);
        const std::size_t sampleH = SampleHeight(view.height, gridSize);

        try {
            outDepthMap.resize(sampleW * sampleH);
        } catch (...) {
            return false;
        }

        // Fast depth map: one sample per cell, taken from cell center.
        // TODO: average/coverage-weighted sampling if desired.
        for (std::size_t sy = 0; sy < sampleH; ++sy) {
            const std::uint32_t baseY = static_cast<std::uint32_t>(sy * gridSize);
            const std::uint32_t centerY = ClampU32(
                baseY + (gridSize / 2u),
                0u,
                view.height - 1u
            );

            for (std::size_t sx = 0; sx < sampleW; ++sx) {
                const std::uint32_t baseX = static_cast<std::uint32_t>(sx * gridSize);
                const std::uint32_t centerX = ClampU32(
                    baseX + (gridSize / 2u),
                    0u,
                    view.width - 1u
                );

                std::uint8_t r = 0, g = 0, b = 0, a = 255;
                if (!ReadPixelRGBA8(view, centerX, centerY, r, g, b, a)) {
                    return false;
                }

                // Ignore alpha by treating fully transparent as zero depth.
                // TODO: add background masking / custom rules here.
                float depth = (a == 0) ? 0.0f : (LumaFromRGBA(r, g, b) / 255.0f);

                depth = Clamp01(depth);
                outDepthMap[sy * sampleW + sx] = depth;
            }
        }

        return true;
    }

    // ----------------------------------------------------------------
    // Color modes for the canvas-layer depth visualisation.
    // ----------------------------------------------------------------
    enum class ColorMode {
        Grayscale  = 0,  // black (far) → white (near)
        FalseColor = 1,  // spectral: dark violet → blue → teal → green → amber → red-pink
        WarmTone   = 2,  // pink/orange/purple: dark purple → magenta → orange → warm yellow
    };

    // Build a full-canvas-resolution RGBA depth map (one output pixel per
    // source pixel) and return it as a flat vector of pelpaint::Pixel.
    // Transparent source pixels are kept transparent in the output.
    static bool BuildDepthMapRGBA(
        const pelpaint::ImageView& view,
        ColorMode                  mode,
        bool                       invert,
        std::vector<pelpaint::Pixel>& outPixels)
    {
        outPixels.clear();
        if (!view.valid() || view.data == nullptr || view.channels < 4) return false;

        const std::size_t total =
            static_cast<std::size_t>(view.width) * view.height;
        outPixels.resize(total);

        // ---- color-stop tables (t, r, g, b) -------------------------
        struct Stop { float t; uint8_t r, g, b; };

        static constexpr Stop kGray[] = {
            {0.00f,   0,   0,   0},
            {1.00f, 255, 255, 255},
        };
        static constexpr Stop kFalse[] = {
            {0.00f,  48,  18,  59},
            {0.15f,  65, 100, 200},
            {0.35f,  40, 190, 165},
            {0.55f, 125, 220,  50},
            {0.75f, 240, 185,  20},
            {0.90f, 245,  80,  25},
            {1.00f, 195,  20,  65},
        };
        static constexpr Stop kWarm[] = {
            {0.00f,  10,   0,  30},
            {0.20f,  60,   0, 100},
            {0.45f, 185,  20, 120},
            {0.65f, 230,  80,  20},
            {0.85f, 255, 180,  30},
            {1.00f, 255, 240, 120},
        };

        const Stop* stops  = kGray;
        int         nStops = 2;
        if (mode == ColorMode::FalseColor) { stops = kFalse; nStops = 7; }
        else if (mode == ColorMode::WarmTone) { stops = kWarm;  nStops = 6; }

        auto sampleRamp = [&](float t) -> pelpaint::Pixel {
            t = Clamp01(t);
            for (int i = 1; i < nStops; ++i) {
                if (t <= stops[i].t) {
                    const float span = stops[i].t - stops[i - 1].t;
                    const float u    = (span > 0.f)
                                       ? (t - stops[i - 1].t) / span : 0.f;
                    return pelpaint::Pixel{
                        static_cast<uint8_t>(stops[i-1].r + u*(stops[i].r - stops[i-1].r)),
                        static_cast<uint8_t>(stops[i-1].g + u*(stops[i].g - stops[i-1].g)),
                        static_cast<uint8_t>(stops[i-1].b + u*(stops[i].b - stops[i-1].b)),
                        255
                    };
                }
            }
            return pelpaint::Pixel{stops[nStops-1].r,
                                   stops[nStops-1].g,
                                   stops[nStops-1].b, 255};
        };

        for (std::uint32_t y = 0; y < view.height; ++y) {
            for (std::uint32_t x = 0; x < view.width; ++x) {
                uint8_t r = 0, g = 0, b = 0, a = 255;
                ReadPixelRGBA8(view, x, y, r, g, b, a);

                if (a == 0) {
                    // Fully transparent — preserve transparency.
                    outPixels[y * view.width + x] = pelpaint::Pixel{0, 0, 0, 0};
                    continue;
                }

                float depth = Clamp01(LumaFromRGBA(r, g, b) / 255.f);
                if (invert) depth = 1.f - depth;

                pelpaint::Pixel px = sampleRamp(depth);
                px.a = a;   // keep source alpha
                outPixels[y * view.width + x] = px;
            }
        }
        return true;
    }
  };
} // namespace pelpaint::exporter
