#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <expected>

#include "../core/Error.hpp"
#include "../ColorPalettes.hpp"
#include "../core/PaletteRef.hpp"

namespace pelpaint::exporter {

struct ProjectLayerData {
    std::string          name;
    float                opacity     = 1.0f;
    bool                 visible     = true;
    bool                 locked      = false;
    int                  zIndex      = 0;
    int                  blendMode   = 0;
    float                blendColorR = 1.0f;
    float                blendColorG = 1.0f;
    float                blendColorB = 1.0f;
    float                blendColorA = 1.0f;
    std::vector<uint8_t> pixelData;  ///< PNG-compressed RGBA8.
};

struct ProjectFrameData {
    float                delay = 0.0f;
    std::string          label;
    std::vector<uint8_t> pixelData;  ///< PNG-compressed RGBA8.
};

/// Complete serialisable project snapshot.
struct ProjectState {
    uint32_t canvasWidth      = 0;
    uint32_t canvasHeight     = 0;
    int      activeLayerIndex = 0;

    std::vector<ProjectLayerData> layers;

    uint8_t colorR = 0, colorG = 0, colorB = 0, colorA = 255;

    int   brushMode        = 0;
    float brushSize        = 1.0f;
    float brushOpacity     = 1.0f;
    bool  brushAntialiased = false;

    uint8_t            palSource         = static_cast<uint8_t>(PaletteSource::Named);
    int32_t            paletteNamedIndex = 0;
    int32_t            maxAutoColors     = 256;
    std::vector<Pixel> customPalette;    ///< Populated when palSource == Custom.

    int  gridMode   = 0;
    int  gridSize   = 8;
    bool snapToGrid = false;

    float animFps     = 12.0f;
    bool  animLooping = true;
    std::vector<ProjectFrameData> animFrames;
};

class ProjectSerializer {
public:
    static constexpr uint32_t kMagic   = 0x4C585050u; // "PPXL" LE
    static constexpr uint32_t kVersion = 2u;

    [[nodiscard]]
    static std::expected<void, pelpaint::Error>
    Save(const std::string& filename, const ProjectState& state);

    [[nodiscard]]
    static std::expected<ProjectState, pelpaint::Error>
    Load(const std::string& filename);
};

} // namespace pelpaint::exporter
