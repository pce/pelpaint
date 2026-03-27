#pragma once

#include <cstdint>
#include <vector>
#include <utility>
#include <algorithm>

namespace pelpaint::core {

enum class PixelFormat {
    RGBA8,
};

struct ImageView {
    const std::uint8_t* data = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    PixelFormat format = PixelFormat::RGBA8;

    constexpr bool valid() const noexcept {
        return data != nullptr && width > 0 && height > 0 && stride > 0;
    }
};

struct PixelRGBA8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    constexpr bool isTransparent() const noexcept {
        return r == 0 && g == 0 && b == 0 && a == 0;
    }
};

class ImageSurface {
public:
    static constexpr std::uint32_t TileSize = 64;

    ImageSurface() = default;
    ImageSurface(std::uint32_t width, std::uint32_t height);

    void Resize(std::uint32_t width, std::uint32_t height);
    void Clear(PixelRGBA8 color = {0, 0, 0, 0});

    std::uint32_t Width() const noexcept { return m_width; }
    std::uint32_t Height() const noexcept { return m_height; }

    bool IsValidCoord(std::uint32_t x, std::uint32_t y) const noexcept;
    PixelRGBA8 GetPixel(std::uint32_t x, std::uint32_t y) const noexcept;
    void SetPixel(std::uint32_t x, std::uint32_t y, PixelRGBA8 color);

    bool HasTile(std::uint32_t tx, std::uint32_t ty) const noexcept;
    bool IsTileDirty(std::uint32_t tx, std::uint32_t ty) const noexcept;

    // Returns a view for a specific tile if allocated.
    bool GetTileView(std::uint32_t tx, std::uint32_t ty, ImageView& outView) const noexcept;

    // Collect all dirty tiles for incremental GPU uploads.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> CollectDirtyTiles() const;
    void ClearDirtyFlags();

    // Creates a contiguous view for processing/export.
    // The view may point to internal scratch storage.
    ImageView Flatten() const;

private:
    struct Tile {
        std::vector<PixelRGBA8> pixels;
        bool allocated = false;
        bool dirty = false;
    };

    static std::uint32_t TileCountX(std::uint32_t width) noexcept;
    static std::uint32_t TileCountY(std::uint32_t height) noexcept;
    static std::uint32_t TileIndex(std::uint32_t tx, std::uint32_t ty, std::uint32_t tilesX) noexcept;

    std::uint32_t TileWidth(std::uint32_t tx) const noexcept;
    std::uint32_t TileHeight(std::uint32_t ty) const noexcept;

    Tile& EnsureTile(std::uint32_t tx, std::uint32_t ty);
    const Tile* GetTile(std::uint32_t tx, std::uint32_t ty) const noexcept;

private:
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    std::uint32_t m_tilesX = 0;
    std::uint32_t m_tilesY = 0;
    std::vector<Tile> m_tiles;

    mutable std::vector<PixelRGBA8> m_flattenScratch;
};

} // namespace pelpaint::core
