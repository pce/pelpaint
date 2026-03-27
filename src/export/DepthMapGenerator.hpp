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
  };
} // namespace pelpaint::exporter
