#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <expected>

#include "../core/Error.hpp"
#include "../ColorPalettes.hpp"  // for pelpaint::Pixel

namespace pelpaint::exporter {

// ---------------------------------------------------------------------------
// ProjectLayerData — serializable snapshot of one canvas layer.
// ---------------------------------------------------------------------------
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
    // Raw RGBA8 pixels: canvasWidth * canvasHeight * 4 bytes
    std::vector<uint8_t> pixelData;
};

// ---------------------------------------------------------------------------
// ProjectFrameData — serializable snapshot of one animation frame.
// ---------------------------------------------------------------------------
struct ProjectFrameData {
    float                delay = 0.0f;
    std::string          label;
    // Raw RGBA8 pixels: canvasWidth * canvasHeight * 4 bytes
    std::vector<uint8_t> pixelData;
};

// ---------------------------------------------------------------------------
// ProjectState — complete serializable project snapshot.
// ---------------------------------------------------------------------------
struct ProjectState {
    // Canvas
    uint32_t canvasWidth      = 0;
    uint32_t canvasHeight     = 0;
    int      activeLayerIndex = 0;

    // Layers
    std::vector<ProjectLayerData> layers;

    // Current drawing color
    uint8_t colorR = 0, colorG = 0, colorB = 0, colorA = 255;

    // Brush
    int   brushMode        = 0;   // 0=PixelPerfect, 1=Pen, 2=PixelBrush
    float brushSize        = 1.0f;
    float brushOpacity     = 1.0f;
    bool  brushAntialiased = false;

    // Palette
    int                selectedPaletteIndex = 0;
    bool               paletteEnabled       = false;
    std::vector<Pixel> customPalette;

    // Grid
    int  gridMode   = 0;
    int  gridSize   = 8;
    bool snapToGrid = false;

    // Animation
    float animFps     = 12.0f;
    bool  animLooping = true;
    std::vector<ProjectFrameData> animFrames;
};

// ---------------------------------------------------------------------------
// ProjectSerializer — static save/load helpers.
// ---------------------------------------------------------------------------
class ProjectSerializer {
public:
    static constexpr uint32_t kMagic   = 0x4C585050u; // "PPXL" stored LE
    static constexpr uint32_t kVersion = 1u;

    [[nodiscard]]
    static std::expected<void, pelpaint::Error>
    Save(const std::string& filename, const ProjectState& state);

    [[nodiscard]]
    static std::expected<ProjectState, pelpaint::Error>
    Load(const std::string& filename);
};

} // namespace pelpaint::exporter
