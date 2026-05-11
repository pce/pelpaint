#pragma once

#include <span>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cstddef>

#include "Types.hpp"
#include "ImageSurface.hpp"

namespace pelpaint {

class Canvas {
public:

    Canvas(int w, int h);

    Canvas(const Canvas&)            = delete;
    Canvas& operator=(const Canvas&) = delete;
    Canvas(Canvas&&)                 = default;
    Canvas& operator=(Canvas&&)      = default;

    [[nodiscard]] int Width()  const noexcept { return width_;  }
    [[nodiscard]] int Height() const noexcept { return height_; }

    // Initialise the default two-layer stack (Background + Foreground).
    void InitDefaultLayers();

    void AddLayer(std::string_view name = "");
    void RemoveLayer(int index);
    void ReorderLayers(int from, int to);

    [[nodiscard]] Layer*       ActiveLayer()       noexcept;
    [[nodiscard]] const Layer* ActiveLayer() const noexcept;

    [[nodiscard]] int  ActiveLayerIndex() const noexcept { return activeLayerIndex_; }
    void               SetActiveLayer(int i) noexcept;

    // Direct (reference) access to the layer vector for LayerPanel widget
    // and CanvasSnapshot serialisation.  Avoid mutation from outside Canvas
    // where possible; prefer the explicit helpers above.
    [[nodiscard]] std::vector<Layer>&       Layers()       noexcept { return layers_; }
    [[nodiscard]] const std::vector<Layer>& Layers() const noexcept { return layers_; }

    /// Allocate and return the next unique layer ID (increments the internal
    /// counter).  Use this instead of the old NextLayerIdRef() accessor.
    int AllocLayerId() noexcept { return nextLayerId_++; }

    /**
     * @brief Write a pixel to the active layer only.
     *
     * Does NOT composite — call Composite() once per stroke/operation end
     * (or rely on the per-frame dirty check in PixelPaintApp::Draw()).
     */
    void                    PutPixel(int x, int y, const Pixel& color) noexcept;
    [[nodiscard]] Pixel     GetPixel(int x, int y)                const noexcept;
    [[nodiscard]] bool      IsValidCoord(int x, int y)            const noexcept;
    [[nodiscard]] int       PixelIndex(int x, int y)              const noexcept;

    /// Zero-copy span over the active layer's pixel buffer.
    /// Drawing algorithms (DrawingAlgorithms.hpp) write here directly,
    /// bypassing PutPixel's per-call overhead.
    [[nodiscard]] std::span<Pixel>       ActiveLayerSpan()       noexcept;
    [[nodiscard]] std::span<const Pixel> ActiveLayerSpan() const noexcept;

    /// Blends all visible layers (sorted by zIndex) into compositeSurface_
    /// using direct TilePixelsMutable() writes — no intermediate flat buffer.
    /// Clears the dirty flag on return.
    void Composite();

    // Read-only access to the composite result.
    //   • GPU upload: iterate CollectDirtyTiles() then GetTileView()
    //   • File export: call Flatten() (reuses scratch — no heap alloc)
    [[nodiscard]] const core::ImageSurface& CompositeSurface() const noexcept {
        return compositeSurface_;
    }
    [[nodiscard]] core::ImageSurface& CompositeSurface() noexcept {
        return compositeSurface_;
    }

    /// Set to true by PutPixel and by drawing algorithms via SetDirty().
    /// Cleared by Composite().
    /// PixelPaintApp::Draw() checks this once per frame and calls Composite()
    /// if true — giving live preview with exactly one composite per frame.
    [[nodiscard]] bool IsDirty() const noexcept { return dirty_; }
    void               SetDirty()      noexcept { dirty_ = true; }

    void Resize(int newW, int newH);
    void Clear(const Pixel& color = {0, 0, 0, 255});

    // Restore all state from an undo/redo snapshot.
    void RestoreFromSnapshot(const CanvasSnapshot& snap);

    // Build a snapshot of the current state (for PushUndo).
    [[nodiscard]] CanvasSnapshot MakeSnapshot(std::string_view description = "") const;

private:
    // Alpha-blend src over dst, respecting src.a.
    static void BlendPixel(Pixel& dst, const Pixel& src) noexcept;

    int                width_           = 0;
    int                height_          = 0;
    std::vector<Layer> layers_;
    int                activeLayerIndex_ = 0;
    int                nextLayerId_      = 0;

    // THE single composite store — replaces the old canvasData vector and
    // the separate canvasSurface member that used to live in PixelPaintView.
    core::ImageSurface compositeSurface_;

    bool               dirty_ = false;
};

} // namespace pelpaint
