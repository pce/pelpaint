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
    float         depthScale     = 1.0f;
    bool          useVertexColors = true;
    bool          optimizeMesh   = true;
    /// When true, all vertices use Z=0 (pure flat 2-D mesh). Only Plane mode uses luma-based depth.
    bool          flatZ           = true;
    /// Constant Z height for solid pixels when flatZ=false and mode != Plane (reserved).
    float         solidDepth      = 0.5f;
    /// Pixels with alpha below this threshold are treated as fully transparent.
    std::uint8_t  alphaThreshold  = 10;
};

class MeshExporter {
public:
    /// Top-level entry point — builds the appropriate mesh and writes a PLY file.
    static bool SaveAsMesh(
        const std::string&            filename,
        const pelpaint::ImageView&    view,
        const pelpaint::ColorPalette& palette,
        const MeshExportOptions&      options);

    // ---- Individual builders (public for testing) -----------------------

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
