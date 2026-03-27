#include "Canvas.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>

// Verify binary layout compatibility between pelpaint::Pixel and core::PixelRGBA8.
// Both are plain 4-byte RGBA structs — safe to reinterpret between them.
static_assert(sizeof(pelpaint::Pixel)        == sizeof(pelpaint::core::PixelRGBA8),
              "Pixel and PixelRGBA8 must have the same size");
static_assert(alignof(pelpaint::Pixel)       == alignof(pelpaint::core::PixelRGBA8),
              "Pixel and PixelRGBA8 must have the same alignment");

namespace pelpaint {

// ============================================================
// Construction
// ============================================================

Canvas::Canvas(int w, int h)
    : width_(w)
    , height_(h)
    , compositeSurface_(static_cast<std::uint32_t>(w),
                        static_cast<std::uint32_t>(h))
{
    InitDefaultLayers();
}

// ============================================================
// Layer management
// ============================================================

void Canvas::InitDefaultLayers()
{
    layers_.clear();

    // Background layer — opaque, z-index 0
    Layer bg("Background", width_, height_, 0);
    bg.opacity = 1.0f;
    layers_.push_back(std::move(bg));

    // Foreground layer — transparent by default, z-index 2
    Layer fg("Foreground", width_, height_, 2);
    fg.opacity = 1.0f;
    layers_.push_back(std::move(fg));

    activeLayerIndex_ = 1;   // foreground is the default drawing surface
    nextLayerId_      = 3;
    dirty_            = true;
}

void Canvas::AddLayer(std::string_view name)
{
    const int zIndex = nextLayerId_++;
    const std::string layerName =
        name.empty() ? ("Layer_" + std::to_string(zIndex)) : std::string(name);

    Layer newLayer(layerName, width_, height_, zIndex);
    layers_.push_back(std::move(newLayer));
    activeLayerIndex_ = static_cast<int>(layers_.size()) - 1;
    dirty_            = true;
}

void Canvas::RemoveLayer(int index)
{
    if (index < 0 || index >= static_cast<int>(layers_.size())) return;
    if (layers_.size() <= 1) return;   // always keep at least one layer

    layers_.erase(layers_.begin() + index);
    if (activeLayerIndex_ >= static_cast<int>(layers_.size()))
        activeLayerIndex_ = static_cast<int>(layers_.size()) - 1;
    dirty_ = true;
}

void Canvas::ReorderLayers(int from, int to)
{
    const int n = static_cast<int>(layers_.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;

    Layer tmp = std::move(layers_[from]);
    if (from < to)
        for (int i = from; i < to; ++i) layers_[i] = std::move(layers_[i + 1]);
    else
        for (int i = from; i > to; --i) layers_[i] = std::move(layers_[i - 1]);
    layers_[to]      = std::move(tmp);
    activeLayerIndex_ = to;
    dirty_            = true;
}

Layer* Canvas::ActiveLayer() noexcept
{
    if (activeLayerIndex_ < 0 || activeLayerIndex_ >= static_cast<int>(layers_.size()))
        return nullptr;
    return &layers_[activeLayerIndex_];
}

const Layer* Canvas::ActiveLayer() const noexcept
{
    if (activeLayerIndex_ < 0 || activeLayerIndex_ >= static_cast<int>(layers_.size()))
        return nullptr;
    return &layers_[activeLayerIndex_];
}

void Canvas::SetActiveLayer(int i) noexcept
{
    if (i >= 0 && i < static_cast<int>(layers_.size()))
        activeLayerIndex_ = i;
}

// ============================================================
// Pixel access
// ============================================================

void Canvas::PutPixel(int x, int y, const Pixel& color) noexcept
{
    if (!IsValidCoord(x, y)) return;

    Layer* layer = ActiveLayer();
    if (!layer || layer->locked) return;

    BlendPixel(layer->pixelData[static_cast<std::size_t>(PixelIndex(x, y))], color);
    dirty_ = true;
}

Pixel Canvas::GetPixel(int x, int y) const noexcept
{
    if (!IsValidCoord(x, y)) return {};
    const Layer* layer = ActiveLayer();
    if (!layer) return {};
    return layer->pixelData[static_cast<std::size_t>(PixelIndex(x, y))];
}

bool Canvas::IsValidCoord(int x, int y) const noexcept
{
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}

int Canvas::PixelIndex(int x, int y) const noexcept
{
    return y * width_ + x;
}

std::span<Pixel> Canvas::ActiveLayerSpan() noexcept
{
    Layer* layer = ActiveLayer();
    if (!layer) return {};
    return std::span<Pixel>(layer->pixelData.data(), layer->pixelData.size());
}

std::span<const Pixel> Canvas::ActiveLayerSpan() const noexcept
{
    const Layer* layer = ActiveLayer();
    if (!layer) return {};
    return std::span<const Pixel>(layer->pixelData.data(), layer->pixelData.size());
}

// ============================================================
// Composite
//
// Blends all visible layers (sorted by zIndex) into compositeSurface_
// using direct TilePixelsMutable() writes — no flat intermediate buffer.
//
// Background colour (dark grey 30,30,30) is written first, then each
// visible layer is alpha-blended on top.
// ============================================================

void Canvas::Composite()
{
    if (width_ <= 0 || height_ <= 0) { dirty_ = false; return; }

    // Sort layer pointers by ascending zIndex (stable, so equal z keeps order).
    std::vector<const Layer*> sorted;
    sorted.reserve(layers_.size());
    for (const auto& l : layers_) sorted.push_back(&l);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Layer* a, const Layer* b) {
                         return a->zIndex < b->zIndex;
                     });

    const std::uint32_t tilesX = compositeSurface_.TilesX();
    const std::uint32_t tilesY = compositeSurface_.TilesY();

    for (std::uint32_t ty = 0; ty < tilesY; ++ty) {
        for (std::uint32_t tx = 0; tx < tilesX; ++tx) {

            const std::uint32_t tw     = compositeSurface_.TileWidth(tx);
            const std::uint32_t th     = compositeSurface_.TileHeight(ty);
            if (tw == 0 || th == 0) continue;

            // Obtain a writable span directly into the tile's pixel buffer.
            // TilePixelsMutable() allocates the tile if needed and marks it dirty.
            auto tileSpan = compositeSurface_.TilePixelsMutable(tx, ty);

            const std::uint32_t originX = tx * core::ImageSurface::TileSize;
            const std::uint32_t originY = ty * core::ImageSurface::TileSize;

            for (std::uint32_t ly = 0; ly < th; ++ly) {
                for (std::uint32_t lx = 0; lx < tw; ++lx) {

                    const int px = static_cast<int>(originX + lx);
                    const int py = static_cast<int>(originY + ly);
                    const int flatIdx = py * width_ + px;

                    // Start with the canvas background colour.
                    uint8_t r = 30, g = 30, b = 30, a = 255;

                    // Alpha-blend each visible layer from bottom to top.
                    for (const Layer* layer : sorted) {
                        if (!layer->visible || layer->pixelData.empty()) continue;

                        const Pixel& src =
                            layer->pixelData[static_cast<std::size_t>(flatIdx)];
                        if (src.a == 0) continue;

                        const float alpha = (src.a / 255.0f) * layer->opacity;
                        const float inv   = 1.0f - alpha;

                        r = static_cast<uint8_t>(r * inv + src.r * alpha);
                        g = static_cast<uint8_t>(g * inv + src.g * alpha);
                        b = static_cast<uint8_t>(b * inv + src.b * alpha);
                        a = std::max(a, static_cast<uint8_t>(src.a * layer->opacity));
                    }

                    // Write zero-copy into the tile buffer.
                    // Tile layout: rows of TileSize pixels; edge tiles have
                    // right/bottom padding that is never uploaded.
                    const std::size_t tileIdx =
                        core::ImageSurface::LocalIndex(lx, ly);
                    tileSpan[tileIdx] = { r, g, b, a };
                }
            }
        }
    }

    dirty_ = false;
}

// ============================================================
// Canvas-level operations
// ============================================================

void Canvas::Resize(int newW, int newH)
{
    if (newW <= 0 || newH <= 0) return;

    for (auto& layer : layers_) {
        std::vector<Pixel> newData(
            static_cast<std::size_t>(newW) * newH, Pixel{0, 0, 0, 0});

        const int copyW = std::min(width_,  newW);
        const int copyH = std::min(height_, newH);

        for (int y = 0; y < copyH; ++y) {
            for (int x = 0; x < copyW; ++x) {
                newData[static_cast<std::size_t>(y) * newW + x] =
                    layer.pixelData[static_cast<std::size_t>(y) * width_ + x];
            }
        }
        layer.pixelData = std::move(newData);
    }

    width_  = newW;
    height_ = newH;
    compositeSurface_.Resize(static_cast<std::uint32_t>(newW),
                              static_cast<std::uint32_t>(newH));
    dirty_ = true;
}

void Canvas::Clear(const Pixel& color)
{
    Layer* layer = ActiveLayer();
    if (layer) {
        std::fill(layer->pixelData.begin(), layer->pixelData.end(), color);
        dirty_ = true;
    }
}

void Canvas::RestoreFromSnapshot(const CanvasSnapshot& snap)
{
    layers_           = snap.layers;
    activeLayerIndex_ = snap.activeLayerIndex;

    if (snap.canvasWidth  != width_ || snap.canvasHeight != height_) {
        width_  = snap.canvasWidth;
        height_ = snap.canvasHeight;
        compositeSurface_.Resize(static_cast<std::uint32_t>(width_),
                                  static_cast<std::uint32_t>(height_));
    }
    dirty_ = true;
}

CanvasSnapshot Canvas::MakeSnapshot(std::string_view description) const
{
    return CanvasSnapshot{ layers_, activeLayerIndex_, width_, height_, description };
}

// ============================================================
// Private helpers
// ============================================================

void Canvas::BlendPixel(Pixel& dst, const Pixel& src) noexcept
{
    if (src.a == 0) {
        dst = src;   // fully transparent: overwrite (erase)
    } else if (src.a == 255) {
        dst = src;   // fully opaque: simple overwrite
    } else {
        const float alpha = src.a / 255.0f;
        const float inv   = 1.0f - alpha;
        dst.r = static_cast<uint8_t>(src.r * alpha + dst.r * inv);
        dst.g = static_cast<uint8_t>(src.g * alpha + dst.g * inv);
        dst.b = static_cast<uint8_t>(src.b * alpha + dst.b * inv);
        dst.a = static_cast<uint8_t>(
            std::min(255, static_cast<int>(dst.a) + static_cast<int>(src.a)));
    }
}

} // namespace pelpaint
