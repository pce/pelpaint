#include "ImageSurface.hpp"

namespace pelpaint::core {

ImageSurface::ImageSurface(std::uint32_t width, std::uint32_t height) {
    Resize(width, height);
}

void ImageSurface::Resize(std::uint32_t width, std::uint32_t height) {
    m_width = width;
    m_height = height;
    m_tilesX = TileCountX(width);
    m_tilesY = TileCountY(height);
    m_tiles.clear();
    m_tiles.resize(static_cast<std::size_t>(m_tilesX) * m_tilesY);
    m_flattenScratch.clear();
}

void ImageSurface::Clear(PixelRGBA8 color) {
    if (color.isTransparent()) {
        for (auto& tile : m_tiles) {
            tile.pixels.clear();
            tile.allocated = false;
            tile.dirty = false;
        }
        return;
    }

    for (std::uint32_t ty = 0; ty < m_tilesY; ++ty) {
        for (std::uint32_t tx = 0; tx < m_tilesX; ++tx) {
            Tile& tile = EnsureTile(tx, ty);
            std::fill(tile.pixels.begin(), tile.pixels.end(), color);
            tile.dirty = true;
        }
    }
}

bool ImageSurface::IsValidCoord(std::uint32_t x, std::uint32_t y) const noexcept {
    return x < m_width && y < m_height;
}

PixelRGBA8 ImageSurface::GetPixel(std::uint32_t x, std::uint32_t y) const noexcept {
    if (!IsValidCoord(x, y)) {
        return {0, 0, 0, 0};
    }

    const std::uint32_t tx = x / TileSize;
    const std::uint32_t ty = y / TileSize;

    const Tile* tile = GetTile(tx, ty);
    if (!tile || !tile->allocated) {
        return {0, 0, 0, 0};
    }

    const std::uint32_t lx = x - (tx * TileSize);
    const std::uint32_t ly = y - (ty * TileSize);
    const std::size_t idx = static_cast<std::size_t>(ly) * TileSize + lx;

    if (idx >= tile->pixels.size()) {
        return {0, 0, 0, 0};
    }

    return tile->pixels[idx];
}

void ImageSurface::SetPixel(std::uint32_t x, std::uint32_t y, PixelRGBA8 color) {
    if (!IsValidCoord(x, y)) return;

    const std::uint32_t tx = x / TileSize;
    const std::uint32_t ty = y / TileSize;

    Tile& tile = EnsureTile(tx, ty);

    const std::uint32_t lx = x - (tx * TileSize);
    const std::uint32_t ly = y - (ty * TileSize);
    const std::size_t idx = static_cast<std::size_t>(ly) * TileSize + lx;

    if (idx < tile.pixels.size()) {
        tile.pixels[idx] = color;
        tile.dirty = true;
    }
}

bool ImageSurface::HasTile(std::uint32_t tx, std::uint32_t ty) const noexcept {
    const Tile* tile = GetTile(tx, ty);
    return tile != nullptr && tile->allocated;
}

bool ImageSurface::IsTileDirty(std::uint32_t tx, std::uint32_t ty) const noexcept {
    const Tile* tile = GetTile(tx, ty);
    return tile != nullptr && tile->allocated && tile->dirty;
}

bool ImageSurface::GetTileView(std::uint32_t tx, std::uint32_t ty, ImageView& outView) const noexcept {
    const Tile* tile = GetTile(tx, ty);
    if (!tile || !tile->allocated || tile->pixels.empty()) {
        outView = {};
        return false;
    }

    const std::uint32_t w = TileWidth(tx);
    const std::uint32_t h = TileHeight(ty);

    if (w == 0 || h == 0) {
        outView = {};
        return false;
    }

    outView.data = reinterpret_cast<const std::uint8_t*>(tile->pixels.data());
    outView.width = w;
    outView.height = h;
    outView.stride = TileSize * sizeof(PixelRGBA8);
    outView.format = PixelFormat::RGBA8;
    return true;
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> ImageSurface::CollectDirtyTiles() const {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> result;
    result.reserve(m_tiles.size());

    for (std::uint32_t ty = 0; ty < m_tilesY; ++ty) {
        for (std::uint32_t tx = 0; tx < m_tilesX; ++tx) {
            const Tile* tile = GetTile(tx, ty);
            if (tile && tile->allocated && tile->dirty) {
                result.emplace_back(tx, ty);
            }
        }
    }

    return result;
}

void ImageSurface::ClearDirtyFlags() {
    for (auto& tile : m_tiles) {
        tile.dirty = false;
    }
}

ImageView ImageSurface::Flatten() const {
    if (m_width == 0 || m_height == 0) return {};

    const std::size_t total = static_cast<std::size_t>(m_width) * m_height;
    m_flattenScratch.assign(total, PixelRGBA8{0, 0, 0, 0});

    for (std::uint32_t y = 0; y < m_height; ++y) {
        for (std::uint32_t x = 0; x < m_width; ++x) {
            const PixelRGBA8 px = GetPixel(x, y);
            m_flattenScratch[static_cast<std::size_t>(y) * m_width + x] = px;
        }
    }

    ImageView view;
    view.data = reinterpret_cast<const std::uint8_t*>(m_flattenScratch.data());
    view.width = m_width;
    view.height = m_height;
    view.stride = m_width * sizeof(PixelRGBA8);
    view.format = PixelFormat::RGBA8;
    return view;
}

std::uint32_t ImageSurface::TileCountX(std::uint32_t width) noexcept {
    return (width + TileSize - 1u) / TileSize;
}

std::uint32_t ImageSurface::TileCountY(std::uint32_t height) noexcept {
    return (height + TileSize - 1u) / TileSize;
}

std::uint32_t ImageSurface::TileIndex(std::uint32_t tx, std::uint32_t ty, std::uint32_t tilesX) noexcept {
    return ty * tilesX + tx;
}

std::uint32_t ImageSurface::TileWidth(std::uint32_t tx) const noexcept {
    const std::uint32_t base = tx * TileSize;
    if (base >= m_width) return 0;
    return std::min(TileSize, m_width - base);
}

std::uint32_t ImageSurface::TileHeight(std::uint32_t ty) const noexcept {
    const std::uint32_t base = ty * TileSize;
    if (base >= m_height) return 0;
    return std::min(TileSize, m_height - base);
}

ImageSurface::Tile& ImageSurface::EnsureTile(std::uint32_t tx, std::uint32_t ty) {
    const std::uint32_t idx = TileIndex(tx, ty, m_tilesX);
    Tile& tile = m_tiles[idx];

    if (!tile.allocated) {
        tile.pixels.assign(static_cast<std::size_t>(TileSize) * TileSize, PixelRGBA8{0, 0, 0, 0});
        tile.allocated = true;
        tile.dirty = true;
    }

    return tile;
}

const ImageSurface::Tile* ImageSurface::GetTile(std::uint32_t tx, std::uint32_t ty) const noexcept {
    if (tx >= m_tilesX || ty >= m_tilesY) return nullptr;
    const std::uint32_t idx = TileIndex(tx, ty, m_tilesX);
    return &m_tiles[idx];
}

} // namespace pelpaint::core
