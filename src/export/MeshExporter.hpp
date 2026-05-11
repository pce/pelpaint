#pragma once

#include "../PixelPaintView.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pelpaint::exporter {

enum class MeshMode {
    Plane,      ///< Height-mapped terrain plane (depth map drives Z)
    Wireframe,  ///< Edge wireframe of the Lo-Poly adaptive triangulation
    LoPoly,     ///< Flat-shaded adaptive-diagonal triangulation (lo-poly art style)
    PixelMesh,  ///< Greedy-merged colour-block cuboids (depth-averaged prisms)
};

struct PixelCell {
    bool valid = false;
    float depth = 0.0f;
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
};

struct MergeRect {
    std::uint32_t x = 0, y = 0, w = 0, h = 0;
    PixelCell     cell;
    float         avgDepth = 0.0f;  ///< Average depth across every cell in this rect
};

struct MeshData;

struct MeshExportOptions {
    MeshMode      mode           = MeshMode::Plane;
    std::uint32_t gridSize       = 1;     ///< cell size in pixels (pixel/block size for PixelMesh)
    float         depthScale     = 1.0f;  ///< Raw Z scale factor passed to mesh builders. Controls world-unit height of extruded geometry.
    bool          useVertexColors = true;
    bool          optimizeMesh   = true;
    /// When true, all vertices use Z=0 (pure flat 2-D mesh). Only Plane mode uses luma-based depth.
    bool          flatZ           = true;
    /// Pixels with alpha below this threshold are treated as fully transparent.
    std::uint8_t  alphaThreshold  = 10;

    /// Depth generation strategy.
    enum class DepthMode {
        Luma,       ///< BT.601 brightness → depth (original; bright = near)
        AlphaDist,  ///< Distance-transform from the transparent boundary
                    ///< (interior pixels are highest). Best for voxel/cube export.
    };

    DepthMode depthMode      = DepthMode::Luma;

    /// Max Z height as a fraction of max(canvas_width, canvas_height).
    /// With default 0.5f a square canvas gets Z in [0, 0.5] while XY are in [0, 1].
    float maxZFraction       = 0.5f;

    /// Invert the depth map after generation (0→1, 1→0).
    bool  invertDepth        = false;

    /// If true, pixels whose final depth is below bgThreshold are treated as
    /// transparent/background and skipped by the mesh builders.
    bool  removeBackground   = false;
    float bgThreshold        = 0.12f;  ///< [0,1] depth below this → background

    /// When true the foreground is extruded symmetrically to BOTH sides
    /// of the canvas plane: Z goes from -maxZFraction/2 to +maxZFraction/2
    /// instead of the default 0 → maxZFraction.
    ///
    /// Example: 10×10 pixel art (black box on white BG), maxZFraction = 1.0
    ///   - XY normalized to [0, 1]  (10 px → 1 unit)
    ///   - Z symmetric: [-0.5, +0.5]  (total = 1 unit)
    ///   → Perfect 1×1×1 cube (10×10×10 in pixel units).
    bool  symmetricExtrude   = false;

    /// When true the background colour is detected automatically by
    /// corner-vote (see DetectBackground in ExportUtils.hpp) and any
    /// rect whose colour matches the background within bgColorTolerance
    /// is excluded from the mesh — no need to set removeBackground / bgThreshold.
    bool  autoDetectBackground   = false;
    /// Per-channel colour distance tolerance for autoDetectBackground.
    std::uint8_t bgColorTolerance = 15;
};

class MeshExporter {
public:
    /// Top-level entry point — builds the appropriate mesh and writes a PLY file.
    static bool SaveAsMesh(
        const std::string&            filename,
        const pelpaint::ImageView&    view,
        const pelpaint::ColorPalette& palette,
        const MeshExportOptions&      options);

    /// Plane: continuous height-mapped grid surface (terrain-like).
    static bool BuildPlaneMesh(const pelpaint::ImageView& view,
                               std::span<const float>     depthMap,
                               std::uint32_t              gridSize,
                               float                      depthScale,
                               MeshData&                  outMesh);

    /// Lo-Poly: adaptive-diagonal flat-shaded triangulation.
    /// Each quad chooses its diagonal to minimise intra-triangle colour variance,
    /// producing the characteristic angular lo-poly art look.
    static bool BuildLoPolyMesh(const pelpaint::ImageView& view,
                                std::span<const float>     depthMap,
                                std::uint32_t              gridSize,
                                float                      depthScale,
                                const MeshExportOptions&   options,
                                MeshData&                  outMesh);

    /// Wireframe: edge skeleton of the Lo-Poly triangulation.
    /// Uses the same adaptive-diagonal choice; de-duplicates edges and
    /// writes them as PLY edge elements.
    static bool BuildWireframeMesh(const pelpaint::ImageView& view,
                                   std::span<const float>     depthMap,
                                   std::uint32_t              gridSize,
                                   float                      depthScale,
                                   const MeshExportOptions&   options,
                                   MeshData&                  outMesh);

    /// PixelMesh: greedy-merged same-colour blocks extruded as cuboid prisms.
    /// Depth is the average of all constituent cells — not just the seed cell.
    static bool BuildPixelMesh(const pelpaint::ImageView& view,
                               std::span<const float>     depthMap,
                               std::uint32_t              gridSize,
                               float                      depthScale,
                               const MeshExportOptions&   options,
                               MeshData&                  outMesh);
};

} // namespace pelpaint::exporter
