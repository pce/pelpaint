#include "../PixelPaintView.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pelpaint::exporter {

enum class MeshMode {
    Solid,        // FastDepthViz surface
    Wireframe,    // grid-line representation
    LoPoly,       // simplified angular surface
    PixelPerfect, // merged block/cube mesh by color
};

struct PixelCell {
    bool valid = false;
    float depth = 0.0f;
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
};

struct MergeRect {
    std::uint32_t x = 0, y = 0, w = 0, h = 0;
    PixelCell cell;
};

struct MeshData;

struct MeshExportOptions {
    MeshMode mode = MeshMode::Solid;
    std::uint32_t gridSize = 1;   // cell size in pixels
    float depthScale = 1.0f;
    bool useVertexColors = true;
    bool optimizeMesh = true;
};

class MeshExporter {
public:
    static bool SaveAsMesh(
        const std::string& filename,
        const pelpaint::ImageView& view,
        const pelpaint::ColorPalette& palette,
        const MeshExportOptions& options
    );


    static bool BuildSolidMesh(const pelpaint::ImageView& view,
                               std::span<const float> depthMap,
                               std::uint32_t gridSize,
                               float depthScale,
                               MeshData& outMesh);

    static bool BuildWireframeMesh(const pelpaint::ImageView& view,
                                   std::span<const float> depthMap,
                                   std::uint32_t gridSize,
                                   float depthScale,
                                   MeshData& outMesh);

    static bool BuildPixelPerfectMesh(const pelpaint::ImageView& view,
                                      std::span<const float> depthMap,
                                      std::uint32_t gridSize,
                                      float depthScale,
                                      MeshData& outMesh);
};

} // namespace pelpaint::exporter
