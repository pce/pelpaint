#include "ImageSurface.hpp"

namespace pelpaint::core {

ImageSurface::ImageSurface(std::uint32_t width, std::uint32_t height) {
    Resize(width, height);
}

void ImageSurface::Resize(std::uint32_t width, std::uint32_t height) {
    m_width  = width;
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
            tile.dirty     = false;
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

// ---- Coordinate helpers ------------------------------------------------

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

// ---- Per-pixel access --------------------------------------------------

bool ImageSurface::IsValidCoord(std::uint32_t x, std::uint32_t y) const noexcept {
    return x < m_width && y < m_height;
}

PixelRGBA8 ImageSurface::GetPixel(std::uint32_t x, std::uint32_t y) const noexcept {
    if (!IsValidCoord(x, y)) return {};

    const Tile* tile = GetTile(TileX(x), TileY(y));
    if (!tile || !tile->allocated) return {};

    const std::size_t idx = LocalIndex(LocalX(x), LocalY(y));
    return (idx < tile->pixels.size()) ? tile->pixels[idx] : PixelRGBA8{};
}

void ImageSurface::SetPixel(std::uint32_t x, std::uint32_t y, PixelRGBA8 color) {
    if (!IsValidCoord(x, y)) return;

    Tile& tile = EnsureTile(TileX(x), TileY(y));
    const std::size_t idx = LocalIndex(LocalX(x), LocalY(y));
    if (idx < tile.pixels.size()) {
        tile.pixels[idx] = color;
        tile.dirty = true;
    }
}

// ---- Zero-copy tile access ---------------------------------------------

std::span<PixelRGBA8> ImageSurface::TilePixelsMutable(std::uint32_t tx,
                                                        std::uint32_t ty) {
    Tile& tile = EnsureTile(tx, ty);
    tile.dirty = true;   // caller will write into it — mark dirty up front
    return std::span<PixelRGBA8>(tile.pixels.data(), tile.pixels.size());
}

std::span<const PixelRGBA8> ImageSurface::TilePixels(std::uint32_t tx,
                                                       std::uint32_t ty) const noexcept {
    const Tile* tile = GetTile(tx, ty);
    if (!tile || !tile->allocated || tile->pixels.empty()) return {};
    return std::span<const PixelRGBA8>(tile->pixels.data(), tile->pixels.size());
}

// ---- Dirty tracking ----------------------------------------------------

bool ImageSurface::HasTile(std::uint32_t tx, std::uint32_t ty) const noexcept {
    const Tile* tile = GetTile(tx, ty);
    return tile && tile->allocated;
}

bool ImageSurface::IsTileDirty(std::uint32_t tx, std::uint32_t ty) const noexcept {
    const Tile* tile = GetTile(tx, ty);
    return tile && tile->allocated && tile->dirty;
}

void ImageSurface::MarkTileDirty(std::uint32_t tx, std::uint32_t ty) noexcept {
    Tile* tile = GetTileMut(tx, ty);
    if (tile && tile->allocated) tile->dirty = true;
}

void ImageSurface::MarkAllDirty() noexcept {
    for (auto& tile : m_tiles) {
        if (tile.allocated) tile.dirty = true;
    }
}

std::vector<std::pair<std::uint32_t, std::uint32_t>>
ImageSurface::CollectDirtyTiles() const {
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

void ImageSurface::ClearDirtyFlags() noexcept {
    for (auto& tile : m_tiles) tile.dirty = false;
}

// ---- GPU-upload helpers ------------------------------------------------

bool ImageSurface::GetTileView(std::uint32_t tx, std::uint32_t ty,
                                ImageView& outView) const noexcept {
    const Tile* tile = GetTile(tx, ty);
    if (!tile || !tile->allocated || tile->pixels.empty()) {
        outView = {};
        return false;
    }

    const std::uint32_t w = TileWidth(tx);
    const std::uint32_t h = TileHeight(ty);
    if (w == 0 || h == 0) { outView = {}; return false; }

    outView.data   = reinterpret_cast<const std::uint8_t*>(tile->pixels.data());
    outView.width  = w;
    outView.height = h;
    // stride is always TileSize*4: GL_UNPACK_ROW_LENGTH handles the padding
    outView.stride = TileSize * sizeof(PixelRGBA8);
    outView.format = PixelFormat::RGBA8;
    return true;
}

// Flatten: iterates tiles directly (no per-pixel GetPixel overhead).
// Returns a non-owning view into m_flattenScratch — valid until the next
// call to Flatten() or Resize().
ImageView ImageSurface::Flatten() const {
    if (m_width == 0 || m_height == 0) return {};

    const std::size_t total = static_cast<std::size_t>(m_width) * m_height;
    m_flattenScratch.assign(total, PixelRGBA8{0, 0, 0, 0});

    for (std::uint32_t ty = 0; ty < m_tilesY; ++ty) {
        for (std::uint32_t tx = 0; tx < m_tilesX; ++tx) {
            const Tile* tile = GetTile(tx, ty);
            if (!tile || !tile->allocated) continue;

            const std::uint32_t tw   = TileWidth(tx);
            const std::uint32_t th   = TileHeight(ty);
            const std::uint32_t tox  = tx * TileSize;
            const std::uint32_t toy  = ty * TileSize;

            for (std::uint32_t ly = 0; ly < th; ++ly) {
                for (std::uint32_t lx = 0; lx < tw; ++lx) {
                    const std::size_t srcIdx =
                        static_cast<std::size_t>(ly) * TileSize + lx;
                    const std::size_t dstIdx =
                        static_cast<std::size_t>(toy + ly) * m_width + (tox + lx);
                    m_flattenScratch[dstIdx] = tile->pixels[srcIdx];
                }
            }
        }
    }

    ImageView view;
    view.data   = reinterpret_cast<const std::uint8_t*>(m_flattenScratch.data());
    view.width  = m_width;
    view.height = m_height;
    view.stride = m_width * sizeof(PixelRGBA8);
    view.format = PixelFormat::RGBA8;
    return view;
}

// ---- Private helpers ---------------------------------------------------

std::uint32_t ImageSurface::TileCountX(std::uint32_t width) noexcept {
    return (width  + TileSize - 1u) / TileSize;
}

std::uint32_t ImageSurface::TileCountY(std::uint32_t height) noexcept {
    return (height + TileSize - 1u) / TileSize;
}

std::uint32_t ImageSurface::TileIndex(std::uint32_t tx,
                                       std::uint32_t ty,
                                       std::uint32_t tilesX) noexcept {
    return ty * tilesX + tx;
}

ImageSurface::Tile& ImageSurface::EnsureTile(std::uint32_t tx,
                                               std::uint32_t ty) {
    const std::uint32_t idx = TileIndex(tx, ty, m_tilesX);
    Tile& tile = m_tiles[idx];

    if (!tile.allocated) {
        tile.pixels.assign(static_cast<std::size_t>(TileSize) * TileSize,
                           PixelRGBA8{0, 0, 0, 0});
        tile.allocated = true;
        tile.dirty     = true;
    }
    return tile;
}

const ImageSurface::Tile* ImageSurface::GetTile(std::uint32_t tx,
                                                  std::uint32_t ty) const noexcept {
    if (tx >= m_tilesX || ty >= m_tilesY) return nullptr;
    return &m_tiles[TileIndex(tx, ty, m_tilesX)];
}

ImageSurface::Tile* ImageSurface::GetTileMut(std::uint32_t tx,
                                               std::uint32_t ty) noexcept {
    if (tx >= m_tilesX || ty >= m_tilesY) return nullptr;
    return &m_tiles[TileIndex(tx, ty, m_tilesX)];
}

} // namespace pelpaint::core
