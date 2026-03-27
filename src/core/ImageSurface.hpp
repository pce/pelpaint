#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <utility>
#include <algorithm>

namespace pelpaint::core {

enum class PixelFormat {
    RGBA8,
};

struct ImageView {
    const std::uint8_t* data   = nullptr;
    std::uint32_t       width  = 0;
    std::uint32_t       height = 0;
    std::uint32_t       stride = 0;   // bytes per row
    PixelFormat         format = PixelFormat::RGBA8;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return data != nullptr && width > 0 && height > 0 && stride > 0;
    }
};

// ---------------------------------------------------------------------------
// PixelRGBA8 — 4-byte RGBA pixel, same binary layout as pelpaint::Pixel.
// The two types are layout-compatible; reinterpret_cast between them is safe
// (verified by static_assert in Canvas.cpp).
// ---------------------------------------------------------------------------
struct PixelRGBA8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    [[nodiscard]] constexpr bool isTransparent() const noexcept {
        return r == 0 && g == 0 && b == 0 && a == 0;
    }

    [[nodiscard]] constexpr bool operator==(const PixelRGBA8&) const noexcept = default;
};

// ---------------------------------------------------------------------------
// ImageSurface
//
// Tile-based RGBA8 image store.
//   • Canvas pixels are split into TileSize × TileSize tiles.
//   • Tiles are lazily allocated (transparent pixels require no storage).
//   • Each tile tracks a dirty flag for incremental GPU upload.
//
// Zero-copy access
//   • TilePixelsMutable(tx,ty) — returns std::span<PixelRGBA8> directly into
//     the tile's vector; the tile is allocated and marked dirty automatically.
//   • TilePixels(tx,ty)        — returns std::span<const PixelRGBA8>; returns
//     an empty span for unallocated (all-transparent) tiles.
//   • GetTileView(tx,ty,out)   — fills an ImageView for GPU upload (read-only).
//   • Flatten()                — copies all tiles into a contiguous scratch
//     buffer and returns a non-owning ImageView (no allocation on repeat calls).
// ---------------------------------------------------------------------------

class ImageSurface {
public:
    static constexpr std::uint32_t TileSize = 64;

    ImageSurface() = default;
    ImageSurface(std::uint32_t width, std::uint32_t height);

    // ---- Resize / clear ------------------------------------------------

    void Resize(std::uint32_t width, std::uint32_t height);
    void Clear(PixelRGBA8 color = {0, 0, 0, 0});

    // ---- Dimensions ----------------------------------------------------

    [[nodiscard]] std::uint32_t Width()  const noexcept { return m_width;  }
    [[nodiscard]] std::uint32_t Height() const noexcept { return m_height; }

    [[nodiscard]] std::uint32_t TilesX() const noexcept { return m_tilesX; }
    [[nodiscard]] std::uint32_t TilesY() const noexcept { return m_tilesY; }

    // ---- Coordinate helpers (constexpr, zero overhead) -----------------

    [[nodiscard]] static constexpr std::uint32_t TileX(std::uint32_t px) noexcept {
        return px / TileSize;
    }
    [[nodiscard]] static constexpr std::uint32_t TileY(std::uint32_t py) noexcept {
        return py / TileSize;
    }
    [[nodiscard]] static constexpr std::uint32_t LocalX(std::uint32_t px) noexcept {
        return px % TileSize;
    }
    [[nodiscard]] static constexpr std::uint32_t LocalY(std::uint32_t py) noexcept {
        return py % TileSize;
    }
    [[nodiscard]] static constexpr std::size_t LocalIndex(std::uint32_t lx,
                                                           std::uint32_t ly) noexcept {
        return static_cast<std::size_t>(ly) * TileSize + lx;
    }

    // Actual pixel dimensions of a (possibly edge) tile.
    [[nodiscard]] std::uint32_t TileWidth (std::uint32_t tx) const noexcept;
    [[nodiscard]] std::uint32_t TileHeight(std::uint32_t ty) const noexcept;

    // ---- Per-pixel access (convenience; prefer spans for bulk ops) ------

    [[nodiscard]] bool      IsValidCoord(std::uint32_t x, std::uint32_t y) const noexcept;
    [[nodiscard]] PixelRGBA8 GetPixel(std::uint32_t x, std::uint32_t y)    const noexcept;
    void                     SetPixel(std::uint32_t x, std::uint32_t y, PixelRGBA8 color);

    // ---- Zero-copy tile access -----------------------------------------

    // Returns a writable span over the full TileSize×TileSize pixel buffer of
    // tile (tx, ty).  The tile is allocated if needed and marked dirty.
    // Span length is always TileSize*TileSize; edge tiles carry zero-padding
    // in the right/bottom margin (transparent) that must not be uploaded.
    [[nodiscard]] std::span<PixelRGBA8>       TilePixelsMutable(std::uint32_t tx,
                                                                 std::uint32_t ty);

    // Read-only span.  Returns an empty span for unallocated tiles.
    [[nodiscard]] std::span<const PixelRGBA8> TilePixels(std::uint32_t tx,
                                                          std::uint32_t ty) const noexcept;

    // ---- Dirty tracking ------------------------------------------------

    [[nodiscard]] bool HasTile    (std::uint32_t tx, std::uint32_t ty) const noexcept;
    [[nodiscard]] bool IsTileDirty(std::uint32_t tx, std::uint32_t ty) const noexcept;

    void MarkTileDirty(std::uint32_t tx, std::uint32_t ty) noexcept;
    void MarkAllDirty() noexcept;

    // Collect (tx, ty) pairs for every allocated & dirty tile.
    [[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>>
        CollectDirtyTiles() const;

    void ClearDirtyFlags() noexcept;

    // ---- GPU-upload helpers --------------------------------------------

    // Fill outView with a read-only view into tile (tx, ty)'s pixel buffer.
    // stride is always TileSize*4 (full-width rows, even for edge tiles).
    // Returns false if the tile is unallocated.
    bool GetTileView(std::uint32_t tx, std::uint32_t ty, ImageView& outView) const noexcept;

    // Flatten all tiles into a contiguous scratch buffer.
    // The returned ImageView borrows that buffer — valid until the next call
    // to Flatten() or Resize().  No heap allocation on repeat calls.
    [[nodiscard]] ImageView Flatten() const;

private:
    // ---- Internal tile bookkeeping -------------------------------------

    struct Tile {
        std::vector<PixelRGBA8> pixels;    // TileSize*TileSize entries when allocated
        bool                    allocated = false;
        bool                    dirty     = false;
    };

    static std::uint32_t TileCountX(std::uint32_t width)  noexcept;
    static std::uint32_t TileCountY(std::uint32_t height) noexcept;
    static std::uint32_t TileIndex (std::uint32_t tx,
                                    std::uint32_t ty,
                                    std::uint32_t tilesX) noexcept;

    Tile&       EnsureTile(std::uint32_t tx, std::uint32_t ty);
    const Tile* GetTile   (std::uint32_t tx, std::uint32_t ty) const noexcept;
          Tile* GetTileMut(std::uint32_t tx, std::uint32_t ty)       noexcept;

    std::uint32_t       m_width  = 0;
    std::uint32_t       m_height = 0;
    std::uint32_t       m_tilesX = 0;
    std::uint32_t       m_tilesY = 0;
    std::vector<Tile>   m_tiles;

    mutable std::vector<PixelRGBA8> m_flattenScratch;
};

} // namespace pelpaint::core
