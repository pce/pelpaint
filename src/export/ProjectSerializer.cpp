#include "ProjectSerializer.hpp"

#include <fstream>
#include <cstring>
#include <algorithm>

// stb — use declarations only (implementations live in PixelPaintView.cpp)
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

namespace pelpaint::exporter {

// ============================================================
// Internal helpers
// ============================================================
namespace {

// ---------- BinaryWriter ----------
struct BinaryWriter {
    std::ofstream& f;

    template<typename T>
    bool write(const T& v) {
        f.write(reinterpret_cast<const char*>(&v), sizeof(v));
        return f.good();
    }
    bool writeBytes(const void* data, std::size_t n) {
        f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(n));
        return f.good();
    }
    bool writeString(const std::string& s) {
        uint32_t len = static_cast<uint32_t>(s.size());
        return write(len) && writeBytes(s.data(), len);
    }
    bool writePngBlob(const std::vector<uint8_t>& blob) {
        uint32_t sz = static_cast<uint32_t>(blob.size());
        return write(sz) && writeBytes(blob.data(), sz);
    }
};

// ---------- BinaryReader ----------
struct BinaryReader {
    std::ifstream& f;

    template<typename T>
    bool read(T& v) {
        f.read(reinterpret_cast<char*>(&v), sizeof(v));
        return f.good();
    }
    bool readBytes(void* data, std::size_t n) {
        f.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(n));
        return f.good();
    }
    bool readString(std::string& s) {
        uint32_t len = 0;
        if (!read(len)) return false;
        s.resize(len);
        if (len > 0 && !readBytes(s.data(), len)) return false;
        return true;
    }
    bool readPngBlob(std::vector<uint8_t>& blob) {
        uint32_t sz = 0;
        if (!read(sz)) return false;
        blob.resize(sz);
        if (sz > 0 && !readBytes(blob.data(), sz)) return false;
        return true;
    }
};

// ---------- Compress RGBA pixels to PNG in memory ----------
std::vector<uint8_t> CompressPixelsToPng(
    const std::vector<uint8_t>& rgba,
    uint32_t w,
    uint32_t h)
{
    std::vector<uint8_t> out;
    out.reserve(w * h / 2);  // rough estimate
    auto cb = [](void* ctx, void* data, int sz) {
        auto* buf = static_cast<std::vector<uint8_t>*>(ctx);
        const auto* p = static_cast<const uint8_t*>(data);
        buf->insert(buf->end(), p, p + sz);
    };
    stbi_write_png_to_func(cb, &out,
        static_cast<int>(w),
        static_cast<int>(h),
        4,
        rgba.data(),
        static_cast<int>(w * 4));
    return out;
}

// ---------- Decompress PNG from memory to RGBA pixels ----------
bool DecompressPng(
    const std::vector<uint8_t>& blob,
    uint32_t expectedW,
    uint32_t expectedH,
    std::vector<uint8_t>& outRgba)
{
    if (blob.empty()) return false;
    int w = 0, h = 0, ch = 0;
    stbi_uc* px = stbi_load_from_memory(
        blob.data(), static_cast<int>(blob.size()),
        &w, &h, &ch, 4);
    if (!px) return false;
    if (static_cast<uint32_t>(w) != expectedW ||
        static_cast<uint32_t>(h) != expectedH) {
        stbi_image_free(px);
        return false;
    }
    const std::size_t sz = static_cast<std::size_t>(w) * h * 4;
    outRgba.assign(px, px + sz);
    stbi_image_free(px);
    return true;
}

} // anonymous namespace

// ============================================================
// ProjectSerializer::Save
// ============================================================
std::expected<void, pelpaint::Error>
ProjectSerializer::Save(const std::string& filename, const ProjectState& state)
{
    std::ofstream f(filename, std::ios::binary | std::ios::trunc);
    if (!f) return std::unexpected(pelpaint::Error::FileIO("Cannot open file for writing"));

    BinaryWriter w{f};

    // Header
    if (!w.write(kMagic))   return std::unexpected(pelpaint::Error::FileIO("Write error"));
    if (!w.write(kVersion)) return std::unexpected(pelpaint::Error::FileIO("Write error"));

    // Canvas
    if (!w.write(state.canvasWidth))      return std::unexpected(pelpaint::Error::FileIO("Write error"));
    if (!w.write(state.canvasHeight))     return std::unexpected(pelpaint::Error::FileIO("Write error"));
    if (!w.write(static_cast<int32_t>(state.activeLayerIndex))) return std::unexpected(pelpaint::Error::FileIO("Write error"));

    // Color
    if (!w.write(state.colorR)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    if (!w.write(state.colorG)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    if (!w.write(state.colorB)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    if (!w.write(state.colorA)) return std::unexpected(pelpaint::Error::FileIO("Write error"));

    // Brush
    if (!w.write(static_cast<uint32_t>(state.brushMode))) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    if (!w.write(state.brushSize))        return std::unexpected(pelpaint::Error::FileIO("Write error"));
    if (!w.write(state.brushOpacity))     return std::unexpected(pelpaint::Error::FileIO("Write error"));
    uint8_t anti = state.brushAntialiased ? 1 : 0;
    if (!w.write(anti)) return std::unexpected(pelpaint::Error::FileIO("Write error"));

    // Palette
    if (!w.write(static_cast<int32_t>(state.selectedPaletteIndex))) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    uint8_t palEn = state.paletteEnabled ? 1 : 0;
    if (!w.write(palEn)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    if (!w.write(static_cast<uint32_t>(state.customPalette.size()))) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    for (const auto& px : state.customPalette) {
        if (!w.write(px.r) || !w.write(px.g) || !w.write(px.b) || !w.write(px.a))
            return std::unexpected(pelpaint::Error::FileIO("Write error"));
    }

    // Grid
    if (!w.write(static_cast<int32_t>(state.gridMode)))   return std::unexpected(pelpaint::Error::FileIO("Write error"));
    if (!w.write(static_cast<int32_t>(state.gridSize)))   return std::unexpected(pelpaint::Error::FileIO("Write error"));
    uint8_t snap = state.snapToGrid ? 1 : 0;
    if (!w.write(snap)) return std::unexpected(pelpaint::Error::FileIO("Write error"));

    // Layers
    if (!w.write(static_cast<uint32_t>(state.layers.size()))) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    for (const auto& layer : state.layers) {
        if (!w.writeString(layer.name)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
        if (!w.write(layer.opacity))    return std::unexpected(pelpaint::Error::FileIO("Write error"));
        if (!w.write(static_cast<uint8_t>(layer.visible ? 1 : 0))) return std::unexpected(pelpaint::Error::FileIO("Write error"));
        if (!w.write(static_cast<uint8_t>(layer.locked ? 1 : 0)))  return std::unexpected(pelpaint::Error::FileIO("Write error"));
        if (!w.write(static_cast<int32_t>(layer.zIndex)))    return std::unexpected(pelpaint::Error::FileIO("Write error"));
        if (!w.write(static_cast<int32_t>(layer.blendMode))) return std::unexpected(pelpaint::Error::FileIO("Write error"));
        if (!w.write(layer.blendColorR)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
        if (!w.write(layer.blendColorG)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
        if (!w.write(layer.blendColorB)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
        if (!w.write(layer.blendColorA)) return std::unexpected(pelpaint::Error::FileIO("Write error"));

        auto blob = CompressPixelsToPng(layer.pixelData, state.canvasWidth, state.canvasHeight);
        if (!w.writePngBlob(blob)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    }

    // Animation
    if (!w.write(state.animFps)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    uint8_t loop = state.animLooping ? 1 : 0;
    if (!w.write(loop)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    if (!w.write(static_cast<uint32_t>(state.animFrames.size()))) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    for (const auto& frame : state.animFrames) {
        if (!w.write(frame.delay))       return std::unexpected(pelpaint::Error::FileIO("Write error"));
        if (!w.writeString(frame.label)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
        auto blob = CompressPixelsToPng(frame.pixelData, state.canvasWidth, state.canvasHeight);
        if (!w.writePngBlob(blob)) return std::unexpected(pelpaint::Error::FileIO("Write error"));
    }

    f.close();
    return {};
}

// ============================================================
// ProjectSerializer::Load
// ============================================================
std::expected<ProjectState, pelpaint::Error>
ProjectSerializer::Load(const std::string& filename)
{
    std::ifstream f(filename, std::ios::binary);
    if (!f) return std::unexpected(pelpaint::Error::FileIO("Cannot open project file"));

    BinaryReader r{f};
    ProjectState state;

    // Header
    uint32_t magic = 0, version = 0;
    if (!r.read(magic))   return std::unexpected(pelpaint::Error::FileIO("Read error"));
    if (!r.read(version)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    if (magic != kMagic)       return std::unexpected(pelpaint::Error::FileIO("Not a .ppx project file"));
    if (version != kVersion)   return std::unexpected(pelpaint::Error::FileIO("Unsupported project file version"));

    // Canvas
    if (!r.read(state.canvasWidth))  return std::unexpected(pelpaint::Error::FileIO("Read error"));
    if (!r.read(state.canvasHeight)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    int32_t ali = 0;
    if (!r.read(ali)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    state.activeLayerIndex = ali;

    // Color
    if (!r.read(state.colorR) || !r.read(state.colorG) ||
        !r.read(state.colorB) || !r.read(state.colorA))
        return std::unexpected(pelpaint::Error::FileIO("Read error"));

    // Brush
    uint32_t bm = 0;
    if (!r.read(bm)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    state.brushMode = static_cast<int>(bm);
    if (!r.read(state.brushSize))    return std::unexpected(pelpaint::Error::FileIO("Read error"));
    if (!r.read(state.brushOpacity)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    uint8_t anti = 0;
    if (!r.read(anti)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    state.brushAntialiased = (anti != 0);

    // Palette
    int32_t spi = 0;
    if (!r.read(spi)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    state.selectedPaletteIndex = spi;
    uint8_t palEn = 0;
    if (!r.read(palEn)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    state.paletteEnabled = (palEn != 0);
    uint32_t cpCount = 0;
    if (!r.read(cpCount)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    state.customPalette.resize(cpCount);
    for (auto& px : state.customPalette) {
        if (!r.read(px.r) || !r.read(px.g) || !r.read(px.b) || !r.read(px.a))
            return std::unexpected(pelpaint::Error::FileIO("Read error"));
    }

    // Grid
    int32_t gm = 0, gs = 0;
    if (!r.read(gm)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    if (!r.read(gs)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    state.gridMode = gm;
    state.gridSize = gs;
    uint8_t snap = 0;
    if (!r.read(snap)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    state.snapToGrid = (snap != 0);

    // Layers
    uint32_t layerCount = 0;
    if (!r.read(layerCount)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    state.layers.resize(layerCount);
    for (auto& layer : state.layers) {
        if (!r.readString(layer.name)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
        if (!r.read(layer.opacity))    return std::unexpected(pelpaint::Error::FileIO("Read error"));
        uint8_t vis = 0, lck = 0;
        if (!r.read(vis) || !r.read(lck)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
        layer.visible = (vis != 0);
        layer.locked  = (lck != 0);
        int32_t zi = 0, bm2 = 0;
        if (!r.read(zi) || !r.read(bm2)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
        layer.zIndex    = zi;
        layer.blendMode = bm2;
        if (!r.read(layer.blendColorR) || !r.read(layer.blendColorG) ||
            !r.read(layer.blendColorB) || !r.read(layer.blendColorA))
            return std::unexpected(pelpaint::Error::FileIO("Read error"));

        std::vector<uint8_t> blob;
        if (!r.readPngBlob(blob)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
        if (!DecompressPng(blob, state.canvasWidth, state.canvasHeight, layer.pixelData))
            return std::unexpected(pelpaint::Error::FileIO("Layer pixel data corrupt"));
    }

    // Animation
    if (!r.read(state.animFps)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    uint8_t loop = 0;
    if (!r.read(loop)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    state.animLooping = (loop != 0);
    uint32_t frameCount = 0;
    if (!r.read(frameCount)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
    state.animFrames.resize(frameCount);
    for (auto& frame : state.animFrames) {
        if (!r.read(frame.delay))       return std::unexpected(pelpaint::Error::FileIO("Read error"));
        if (!r.readString(frame.label)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
        std::vector<uint8_t> blob;
        if (!r.readPngBlob(blob)) return std::unexpected(pelpaint::Error::FileIO("Read error"));
        if (!DecompressPng(blob, state.canvasWidth, state.canvasHeight, frame.pixelData))
            return std::unexpected(pelpaint::Error::FileIO("Frame pixel data corrupt"));
    }

    return state;
}

} // namespace pelpaint::exporter
