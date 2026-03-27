#include "PixelPaintView.hpp"
#include "ImGuiFileDialog.h"
#include "export/ImageExporter.hpp"
#include "export/MeshExporter.hpp"
#include "ui/Widgets.hpp"
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <map>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#if defined(USE_METAL_BACKEND)
    #import <Metal/Metal.h>
    #import <QuartzCore/CAMetalLayer.h>
#elif defined(USE_WEBGL_BACKEND)
    #include <SDL3/SDL_opengles2.h>
#elif defined(USE_VULKAN_BACKEND)
    #include <SDL3/SDL_vulkan.h>
#elif defined(USE_OPENGL_BACKEND)
    #include <SDL3/SDL_opengl.h>
#else
    #error "No rendering backend selected"
#endif

#ifdef __APPLE__
    #include <TargetConditionals.h>
    #if TARGET_OS_IOS || TARGET_OS_TV
        #include "IOSFileManager.h"
    #endif
#endif

namespace pelpaint {

using pelpaint::exporter::ImageExporter;
using pelpaint::exporter::MeshExporter;
using pelpaint::exporter::MeshExportOptions;
using pelpaint::exporter::MeshMode;



namespace fs = std::filesystem;

void PixelPaintView::IOSOpenFileCallback(void* context, const char* filepath)
{
    if (!context || !filepath) return;
    auto* view = static_cast<PixelPaintView*>(context);
    view->LoadFromImage(filepath);
}

// Constructor
PixelPaintView::PixelPaintView()
    : canvasData(canvasWidth * canvasHeight, pelpaint::Pixel(30, 30, 30, 255))
    , canvasSize(static_cast<float>(canvasWidth), static_cast<float>(canvasHeight))
    , canvasSurface(static_cast<std::uint32_t>(canvasWidth), static_cast<std::uint32_t>(canvasHeight))
{
    availablePalettes = pelpaint::palettes::GetAllPalettes();
    currentFilename = GetDefaultFilename("png");
    grayscaleToMono = false;

    // Default custom shape: a simple 5x5 filled circle centered in the 8x8 grid
    shapeRedrawCustomMap.fill(false);
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            int dr = row - 3, dc = col - 3;
            if (dr * dr + dc * dc <= 9) {
                shapeRedrawCustomMap[row * 8 + col] = true;
            }
        }
    }

    InitializeTexture();
    InitializeLayers();
    LoadLastDirectory();

    // Initial snapshot after layers are set up
    undoStack.emplace_back(layers, activeLayerIndex, canvasWidth, canvasHeight, "Initial state");
}

// AddSlider and AddSliderFloat removed.
// Use pelpaint::ui::SliderIntStepStateful / SliderFloatStepStateful instead.

// Destructor
PixelPaintView::~PixelPaintView()
{
    DestroyTexture();
}

// Initialize default layer stack: background (z=0) and foreground (z=2)
void PixelPaintView::InitializeLayers()
{
    layers.clear();

    // Create background layer (opaque, z-index 0)
    Layer backgroundLayer("Background", canvasWidth, canvasHeight, 0);
    backgroundLayer.opacity = 1.0f;
    layers.push_back(backgroundLayer);

    // Create foreground layer (transparent, z-index 2, default active)
    Layer foregroundLayer("Foreground", canvasWidth, canvasHeight, 2);
    foregroundLayer.opacity = 1.0f;
    layers.push_back(foregroundLayer);

    activeLayerIndex = 1;  // Foreground is default active (index 1 in the stack)
    nextLayerId = 3;
}

// Add a new layer to the stack
void PixelPaintView::AddLayer(const std::string& name)
{
    int newZIndex = nextLayerId++;
    Layer newLayer(name.empty() ? ("Layer_" + std::to_string(newZIndex)) : name,
                   canvasWidth, canvasHeight, newZIndex);
    layers.push_back(newLayer);
    activeLayerIndex = layers.size() - 1;  // New layer becomes active
}

// Remove layer by index
void PixelPaintView::RemoveLayer(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= static_cast<int>(layers.size())) return;
    if (layers.size() <= 1) return;  // Keep at least one layer

    layers.erase(layers.begin() + layerIndex);
    if (activeLayerIndex >= static_cast<int>(layers.size())) {
        activeLayerIndex = layers.size() - 1;
    }
}

// Reorder layers in the stack
void PixelPaintView::ReorderLayers(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= static_cast<int>(layers.size()) ||
        toIndex < 0 || toIndex >= static_cast<int>(layers.size())) {
        return;
    }

    if (fromIndex == toIndex) return;

    Layer temp = layers[fromIndex];
    if (fromIndex < toIndex) {
        for (int i = fromIndex; i < toIndex; ++i) {
            layers[i] = layers[i + 1];
        }
    } else {
        for (int i = fromIndex; i > toIndex; --i) {
            layers[i] = layers[i - 1];
        }
    }
    layers[toIndex] = temp;
    activeLayerIndex = toIndex;
}

// Composite all visible layers into output buffer
void PixelPaintView::CompositeLayers(std::vector<pelpaint::Pixel>& output) const
{
    output.assign(canvasData.begin(), canvasData.end());

    // Sort layers by z-index and composite from bottom to top
    std::vector<const Layer*> sortedLayers;
    for (const auto& layer : layers) {
        sortedLayers.push_back(&layer);
    }
    std::sort(sortedLayers.begin(), sortedLayers.end(),
              [](const Layer* a, const Layer* b) { return a->zIndex < b->zIndex; });

    for (const auto* layer : sortedLayers) {
        if (!layer->visible) continue;

        for (size_t i = 0; i < output.size(); ++i) {
            const pelpaint::Pixel& layerPixel = layer->pixelData[i];
            if (layerPixel.a == 0) continue;  // Skip transparent pixels

            // Simple alpha blending
            float alpha = (layerPixel.a / 255.0f) * layer->opacity;
            output[i].r = static_cast<uint8_t>(output[i].r * (1.0f - alpha) + layerPixel.r * alpha);
            output[i].g = static_cast<uint8_t>(output[i].g * (1.0f - alpha) + layerPixel.g * alpha);
            output[i].b = static_cast<uint8_t>(output[i].b * (1.0f - alpha) + layerPixel.b * alpha);
            output[i].a = std::max(output[i].a, static_cast<uint8_t>(layerPixel.a * layer->opacity));
        }
    }
}

// Render active layer to canvas (for texture update)
void PixelPaintView::RenderLayerToCanvas()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= static_cast<int>(layers.size())) {
        return;
    }
    CompositeLayers(canvasData);
    if (!dirtyRect.dirty) {
        MarkDirtyFull();
    }
    SyncSurfaceFromCanvas();
    textureNeedsUpdate = true;
}

// Get pointer to active layer
Layer* PixelPaintView::GetActiveLayer()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= static_cast<int>(layers.size())) {
        return nullptr;
    }
    return &layers[activeLayerIndex];
}

// Get const pointer to active layer
const Layer* PixelPaintView::GetActiveLayer() const
{
    if (activeLayerIndex < 0 || activeLayerIndex >= static_cast<int>(layers.size())) {
        return nullptr;
    }
    return &layers[activeLayerIndex];
}

// Initialize texture
void PixelPaintView::InitializeTexture()
{
    textureWidth = 0;
    textureHeight = 0;
    ClearDirtyRect();
#if defined(USE_METAL_BACKEND)
    metalTexture = nullptr;
    textureNeedsUpdate = false;
#else
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    textureNeedsUpdate = true;
#endif
}



#if defined(USE_METAL_BACKEND)
void PixelPaintView::SetMetalDevice(void* device)
{
    metalDevice = device;
    textureNeedsUpdate = true;
}
#endif

// Update texture from canvas data
void PixelPaintView::MarkDirtyRect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;

    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(canvasWidth, x + w);
    int y1 = std::min(canvasHeight, y + h);

    if (x0 >= x1 || y0 >= y1) return;

    if (!dirtyRect.dirty) {
        dirtyRect = {true, x0, y0, x1 - x0, y1 - y0};
        return;
    }

    int nx0 = std::min(dirtyRect.x, x0);
    int ny0 = std::min(dirtyRect.y, y0);
    int nx1 = std::max(dirtyRect.x + dirtyRect.w, x1);
    int ny1 = std::max(dirtyRect.y + dirtyRect.h, y1);

    dirtyRect = {true, nx0, ny0, nx1 - nx0, ny1 - ny0};
}

void PixelPaintView::MarkDirtyFull()
{
    dirtyRect = {true, 0, 0, canvasWidth, canvasHeight};
}

void PixelPaintView::ClearDirtyRect()
{
    dirtyRect = {};
}

void PixelPaintView::UpdateTexture()
{
    if (!textureNeedsUpdate) return;

#if defined(USE_METAL_BACKEND)
    @autoreleasepool {
        id<MTLDevice> device = (__bridge id<MTLDevice>)metalDevice;
        if (!device) {
            std::cerr << "Metal device not set" << std::endl;
            return;
        }

        const bool needsNewTexture = (metalTexture == nullptr) ||
                                     (textureWidth != canvasWidth) ||
                                     (textureHeight != canvasHeight);

        if (needsNewTexture) {
            MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                width:canvasWidth
                height:canvasHeight
                mipmapped:NO];
            textureDescriptor.usage = MTLTextureUsageShaderRead;

            id<MTLTexture> texture = [device newTextureWithDescriptor:textureDescriptor];
            metalTexture = (void*)texture;
            textureWidth = canvasWidth;
            textureHeight = canvasHeight;
            MarkDirtyFull();
        }

        id<MTLTexture> texture = (id<MTLTexture>)metalTexture;

        const int rx = dirtyRect.dirty ? dirtyRect.x : 0;
        const int ry = dirtyRect.dirty ? dirtyRect.y : 0;
        const int rw = dirtyRect.dirty ? dirtyRect.w : canvasWidth;
        const int rh = dirtyRect.dirty ? dirtyRect.h : canvasHeight;

        MTLRegion region = MTLRegionMake2D(rx, ry, rw, rh);
        const pelpaint::Pixel* src = canvasData.data() + (ry * canvasWidth + rx);
        [texture replaceRegion:region mipmapLevel:0 withBytes:src bytesPerRow:canvasWidth * 4];

        textureNeedsUpdate = false;
        ClearDirtyRect();
    }
#else
    glBindTexture(GL_TEXTURE_2D, textureID);

    const bool needsAlloc = (textureWidth != canvasWidth) || (textureHeight != canvasHeight);
    if (needsAlloc) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvasWidth, canvasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, canvasData.data());
        textureWidth = canvasWidth;
        textureHeight = canvasHeight;
        textureNeedsUpdate = false;
        ClearDirtyRect();
        return;
    }

    if (dirtyRect.dirty) {
#if defined(GL_UNPACK_ROW_LENGTH)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, canvasWidth);
#endif
        const pelpaint::Pixel* src = canvasData.data() + (dirtyRect.y * canvasWidth + dirtyRect.x);
        glTexSubImage2D(GL_TEXTURE_2D, 0, dirtyRect.x, dirtyRect.y, dirtyRect.w, dirtyRect.h, GL_RGBA, GL_UNSIGNED_BYTE, src);
#if defined(GL_UNPACK_ROW_LENGTH)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, canvasWidth, canvasHeight, GL_RGBA, GL_UNSIGNED_BYTE, canvasData.data());
    }

    textureNeedsUpdate = false;
    ClearDirtyRect();
#endif
}

void PixelPaintView::SyncSurfaceFromCanvas()
{
    if (canvasWidth <= 0 || canvasHeight <= 0) {
        return;
    }

    if (canvasSurface.Width() != static_cast<std::uint32_t>(canvasWidth) ||
        canvasSurface.Height() != static_cast<std::uint32_t>(canvasHeight)) {
        canvasSurface.Resize(static_cast<std::uint32_t>(canvasWidth),
                             static_cast<std::uint32_t>(canvasHeight));
    }

    if (!dirtyRect.dirty) {
        return;
    }

    const int x0 = dirtyRect.x;
    const int y0 = dirtyRect.y;
    const int x1 = dirtyRect.x + dirtyRect.w;
    const int y1 = dirtyRect.y + dirtyRect.h;

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const pelpaint::Pixel& px = canvasData[static_cast<std::size_t>(y) * canvasWidth + x];
            canvasSurface.SetPixel(
                static_cast<std::uint32_t>(x),
                static_cast<std::uint32_t>(y),
                core::PixelRGBA8{px.r, px.g, px.b, px.a}
            );
        }
    }
}

// Destroy texture
void PixelPaintView::DestroyTexture()
{
#if defined(USE_METAL_BACKEND)
    if (metalTexture != nullptr) {
        id<MTLTexture> texture = (__bridge_transfer id<MTLTexture>)metalTexture;
        metalTexture = nullptr;
    }
#else
    if (textureID != 0) {
        glDeleteTextures(1, &textureID);
        textureID = 0;
    }
#endif
}

// Resize canvas
void PixelPaintView::ResizeCanvas(int newWidth, int newHeight)
{
    // Resize all layers
    for (auto& layer : layers) {
        std::vector<pelpaint::Pixel> newData(newWidth * newHeight, pelpaint::Pixel(0, 0, 0, 0));

        int copyWidth = std::min(canvasWidth, newWidth);
        int copyHeight = std::min(canvasHeight, newHeight);

        if (canvasWidth > 0 && canvasHeight > 0 && !layer.pixelData.empty()) {
            for (int y = 0; y < copyHeight; ++y) {
                for (int x = 0; x < copyWidth; ++x) {
                    newData[y * newWidth + x] = layer.pixelData[y * canvasWidth + x];
                }
            }
        }

        layer.pixelData = std::move(newData);
    }

    canvasWidth = newWidth;
    canvasHeight = newHeight;
    canvasData.assign(canvasWidth * canvasHeight, pelpaint::Pixel(0, 0, 0, 0));

    canvasSurface.Resize(static_cast<std::uint32_t>(canvasWidth),
                         static_cast<std::uint32_t>(canvasHeight));

    RenderLayerToCanvas();
    textureNeedsUpdate = true;
}

// Clear canvas
void PixelPaintView::ClearCanvas(const pelpaint::Pixel& color)
{
    if (activeLayerIndex >= 0 && activeLayerIndex < static_cast<int>(layers.size())) {
        Layer& activeLayer = layers[activeLayerIndex];
        std::fill(activeLayer.pixelData.begin(), activeLayer.pixelData.end(), color);
        RenderLayerToCanvas();
    } else {
        std::fill(canvasData.begin(), canvasData.end(), color);
        MarkDirtyFull();
        SyncSurfaceFromCanvas();
    }
    textureNeedsUpdate = true;
}

// Put pixel
void PixelPaintView::PutPixel(int x, int y, const pelpaint::Pixel& color)
{
    if (!IsValidCoord(x, y)) return;
    if (!IsPointInSelection(x, y)) return;
    if (activeLayerIndex < 0 || activeLayerIndex >= static_cast<int>(layers.size())) return;

    Layer& activeLayer = layers[activeLayerIndex];
    if (activeLayer.locked) return;  // Don't draw on locked layers

    int index = GetPixelIndex(x, y);
    pelpaint::Pixel& pixel = activeLayer.pixelData[index];

    if (color.a == 0) {
        pixel = color;
    } else if (color.a == 255) {
        pixel = color;
    } else {
        float alpha = color.a / 255.0f;
        pixel.r = static_cast<uint8_t>(color.r * alpha + pixel.r * (1 - alpha));
        pixel.g = static_cast<uint8_t>(color.g * alpha + pixel.g * (1 - alpha));
        pixel.b = static_cast<uint8_t>(color.b * alpha + pixel.b * (1 - alpha));
        pixel.a = std::min(255, static_cast<int>(pixel.a) + static_cast<int>(color.a));
    }

    MarkDirtyRect(x, y, 1, 1);
    RenderLayerToCanvas();  // Update canvas from layer composite
}

// Get pixel from active layer
pelpaint::Pixel PixelPaintView::GetPixel(int x, int y) const
{
    if (!IsValidCoord(x, y)) return pelpaint::Pixel(0, 0, 0, 0);
    if (activeLayerIndex < 0 || activeLayerIndex >= static_cast<int>(layers.size())) {
        return canvasData[GetPixelIndex(x, y)];
    }
    return layers[activeLayerIndex].pixelData[GetPixelIndex(x, y)];
}

// Check if coordinate is valid
bool PixelPaintView::IsValidCoord(int x, int y) const
{
    return x >= 0 && x < canvasWidth && y >= 0 && y < canvasHeight;
}

// Get pixel index
int PixelPaintView::GetPixelIndex(int x, int y) const
{
    return y * canvasWidth + x;
}

// Bresenham line drawing
void PixelPaintView::DrawLineBresenham(int x0, int y0, int x1, int y1, const pelpaint::Pixel& color, float brushSize)
{
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    int radius = static_cast<int>(brushSize / 2.0f);

    while (true) {
        DrawCircle(x0, y0, radius, color, true);

        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// Circle drawing
void PixelPaintView::DrawCircle(int cx, int cy, int radius, const pelpaint::Pixel& color, bool filled)
{
    if (filled) {
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x * x + y * y <= radius * radius) {
                    PutPixel(cx + x, cy + y, color);
                }
            }
        }
    } else {
        int x = 0;
        int y = radius;
        int d = 3 - 2 * radius;

        while (x <= y) {
            PutPixel(cx + x, cy + y, color);
            PutPixel(cx - x, cy + y, color);
            PutPixel(cx + x, cy - y, color);
            PutPixel(cx - x, cy - y, color);
            PutPixel(cx + y, cy + x, color);
            PutPixel(cx - y, cy + x, color);
            PutPixel(cx + y, cy - x, color);
            PutPixel(cx - y, cy - x, color);

            if (d < 0) {
                d = d + 4 * x + 6;
            } else {
                d = d + 4 * (x - y) + 10;
                y--;
            }
            x++;
        }
    }
}

// Flood fill
void PixelPaintView::FloodFill(int x, int y, const pelpaint::Pixel& fillColor)
{
    if (!IsValidCoord(x, y)) return;
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer || activeLayer->locked) return;

    pelpaint::Pixel targetColor = GetPixel(x, y);
    if (targetColor.r == fillColor.r && targetColor.g == fillColor.g &&
        targetColor.b == fillColor.b && targetColor.a == fillColor.a) {
        return;
    }

    // Use visited set to prevent infinite loops and redundant processing
    std::vector<bool> visited(canvasWidth * canvasHeight, false);
    std::queue<std::pair<int, int>> queue;
    queue.push({x, y});
    visited[y * canvasWidth + x] = true;

    bool anyWritten = false;
    int minX = x, minY = y, maxX = x, maxY = y;

    // Batch all pixels and composite once at end
    while (!queue.empty()) {
        auto [cx, cy] = queue.front();
        queue.pop();

        if (!IsValidCoord(cx, cy)) continue;
        if (!IsPointInSelection(cx, cy)) continue;

        pelpaint::Pixel currentColor = GetPixel(cx, cy);
        if (currentColor.r != targetColor.r || currentColor.g != targetColor.g ||
            currentColor.b != targetColor.b || currentColor.a != targetColor.a) {
            continue;
        }

        // Write directly to layer without PutPixel overhead
        int index = GetPixelIndex(cx, cy);
        pelpaint::Pixel& pixel = activeLayer->pixelData[index];

        if (fillColor.a == 0) {
            pixel = fillColor;
        } else if (fillColor.a == 255) {
            pixel = fillColor;
        } else {
            float alpha = fillColor.a / 255.0f;
            pixel.r = static_cast<uint8_t>(fillColor.r * alpha + pixel.r * (1 - alpha));
            pixel.g = static_cast<uint8_t>(fillColor.g * alpha + pixel.g * (1 - alpha));
            pixel.b = static_cast<uint8_t>(fillColor.b * alpha + pixel.b * (1 - alpha));
            pixel.a = std::min(255, static_cast<int>(pixel.a) + static_cast<int>(fillColor.a));
        }

        anyWritten = true;
        minX = std::min(minX, cx);
        minY = std::min(minY, cy);
        maxX = std::max(maxX, cx);
        maxY = std::max(maxY, cy);

        // Add unvisited neighbors to queue
        int neighbors[4][2] = {{cx + 1, cy}, {cx - 1, cy}, {cx, cy + 1}, {cx, cy - 1}};
        for (int i = 0; i < 4; ++i) {
            int nx = neighbors[i][0];
            int ny = neighbors[i][1];
            if (IsValidCoord(nx, ny) && IsPointInSelection(nx, ny)) {
                int nindex = GetPixelIndex(nx, ny);
                if (!visited[nindex]) {
                    visited[nindex] = true;
                    queue.push({nx, ny});
                }
            }
        }
    }

    if (!anyWritten) return;
    MarkDirtyRect(minX, minY, maxX - minX + 1, maxY - minY + 1);

    // Composite ONCE after entire fill completes
    RenderLayerToCanvas();
    textureNeedsUpdate = true;
    PushUndo("Flood fill");
}

// Flood fill with threshold
void PixelPaintView::FloodFillWithThreshold(int x, int y, const pelpaint::Pixel& fillColor, float threshold)
{
    if (!IsValidCoord(x, y)) return;
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer || activeLayer->locked) return;

    pelpaint::Pixel targetColor = GetPixel(x, y);
    std::queue<std::pair<int, int>> queue;
    std::vector<bool> visited(canvasWidth * canvasHeight, false);

    queue.push({x, y});
    visited[GetPixelIndex(x, y)] = true;

    bool anyWritten = false;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;

    // Batch all pixels and composite once at end
    while (!queue.empty()) {
        auto [cx, cy] = queue.front();
        queue.pop();

        // Write directly to layer without PutPixel overhead
        if (IsValidCoord(cx, cy) && IsPointInSelection(cx, cy)) {
            int index = GetPixelIndex(cx, cy);
            pelpaint::Pixel& pixel = activeLayer->pixelData[index];

            if (fillColor.a == 0) {
                pixel = fillColor;
            } else if (fillColor.a == 255) {
                pixel = fillColor;
            } else {
                float alpha = fillColor.a / 255.0f;
                pixel.r = static_cast<uint8_t>(fillColor.r * alpha + pixel.r * (1 - alpha));
                pixel.g = static_cast<uint8_t>(fillColor.g * alpha + pixel.g * (1 - alpha));
                pixel.b = static_cast<uint8_t>(fillColor.b * alpha + pixel.b * (1 - alpha));
                pixel.a = std::min(255, static_cast<int>(pixel.a) + static_cast<int>(fillColor.a));
            }

            if (!anyWritten) {
                minX = maxX = cx;
                minY = maxY = cy;
                anyWritten = true;
            } else {
                minX = std::min(minX, cx);
                minY = std::min(minY, cy);
                maxX = std::max(maxX, cx);
                maxY = std::max(maxY, cy);
            }
        }

        int dx[] = {1, -1, 0, 0};
        int dy[] = {0, 0, 1, -1};

        for (int i = 0; i < 4; ++i) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];

            if (!IsValidCoord(nx, ny)) continue;
            if (!IsPointInSelection(nx, ny)) continue;
            if (visited[GetPixelIndex(nx, ny)]) continue;

            pelpaint::Pixel neighborColor = GetPixel(nx, ny);
            float distance = ColorDistance(targetColor, neighborColor);

            if (distance <= threshold) {
                visited[GetPixelIndex(nx, ny)] = true;
                queue.push({nx, ny});
            }
        }
    }

    if (!anyWritten) return;
    MarkDirtyRect(minX, minY, maxX - minX + 1, maxY - minY + 1);

    // Composite ONCE after entire fill completes
    RenderLayerToCanvas();
    textureNeedsUpdate = true;
    PushUndo("Flood fill with threshold");
}

// Spray tool
void PixelPaintView::DrawSpray(int x, int y, float radius, const pelpaint::Pixel& color, float density)
{
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer || activeLayer->locked) return;

    int numDots = static_cast<int>(radius * radius * density);

    // Batch all pixels to avoid compositing overhead
    for (int i = 0; i < numDots; ++i) {
        float angle = static_cast<float>(rand()) / RAND_MAX * 2.0f * 3.14159f;
        float dist = static_cast<float>(rand()) / RAND_MAX * radius;

        int px = x + static_cast<int>(std::cos(angle) * dist);
        int py = y + static_cast<int>(std::sin(angle) * dist);

        // Write directly to layer without PutPixel overhead
        if (IsValidCoord(px, py) && IsPointInSelection(px, py)) {
            int index = GetPixelIndex(px, py);
            pelpaint::Pixel& pixel = activeLayer->pixelData[index];

            if (color.a == 255) {
                pixel = color;
            } else {
                float alpha = color.a / 255.0f;
                pixel.r = static_cast<uint8_t>(color.r * alpha + pixel.r * (1 - alpha));
                pixel.g = static_cast<uint8_t>(color.g * alpha + pixel.g * (1 - alpha));
                pixel.b = static_cast<uint8_t>(color.b * alpha + pixel.b * (1 - alpha));
                pixel.a = std::min(255, static_cast<int>(pixel.a) + static_cast<int>(color.a));
            }
        }
    }

    // Composite ONCE after all spray dots placed
    const int r = static_cast<int>(std::ceil(radius));
    MarkDirtyRect(x - r, y - r, r * 2 + 1, r * 2 + 1);
    RenderLayerToCanvas();
}

// Color distance for dithering and flood fill
float PixelPaintView::ColorDistance(const pelpaint::Pixel& c1, const pelpaint::Pixel& c2) const
{
    int dr = static_cast<int>(c1.r) - static_cast<int>(c2.r);
    int dg = static_cast<int>(c1.g) - static_cast<int>(c2.g);
    int db = static_cast<int>(c1.b) - static_cast<int>(c2.b);
    int da = static_cast<int>(c1.a) - static_cast<int>(c2.a);

    return std::sqrt(dr * dr + dg * dg + db * db + da * da);
}

// Find nearest palette color
pelpaint::Pixel PixelPaintView::FindNearestPaletteColor(const pelpaint::Pixel& color, const std::vector<pelpaint::Pixel>& palette) const
{
    if (palette.empty()) return color;

    pelpaint::Pixel nearest = palette[0];
    float minDistance = ColorDistance(color, palette[0]);

    for (const auto& paletteColor : palette) {
        float distance = ColorDistance(color, paletteColor);
        if (distance < minDistance) {
            minDistance = distance;
            nearest = paletteColor;
        }
    }

    return nearest;
}

void PixelPaintView::DiffuseError(int x, int y, int errorR, int errorG, int errorB, int spreadX, int spreadY, int divisor, int totalWeight)
{
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer) return;

    // Spread the error to neighboring pixels in the active layer
    for (int dy = -spreadY; dy <= spreadY; ++dy) {
        for (int dx = -spreadX; dx <= spreadX; ++dx) {
            if (dx == 0 && dy == 0) continue; // Skip the current pixel

            int nx = x + dx;
            int ny = y + dy;

            if (IsValidCoord(nx, ny)) {
                pelpaint::Pixel& neighbor = activeLayer->pixelData[GetPixelIndex(nx, ny)];
                // Apply clamped error diffusion to avoid overflow/underflow
                neighbor.r = static_cast<uint8_t>(std::clamp(static_cast<int>(neighbor.r) + (errorR * divisor) / totalWeight, 0, 255));
                neighbor.g = static_cast<uint8_t>(std::clamp(static_cast<int>(neighbor.g) + (errorG * divisor) / totalWeight, 0, 255));
                neighbor.b = static_cast<uint8_t>(std::clamp(static_cast<int>(neighbor.b) + (errorB * divisor) / totalWeight, 0, 255));
            }
        }
    }
}


// Apply palette
void PixelPaintView::ApplyPalette(const std::vector<pelpaint::Pixel>& palette)
{
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer) return;

    for (auto& pixel : activeLayer->pixelData) {
        pixel = FindNearestPaletteColor(pixel, palette);
    }
    RenderLayerToCanvas();
    textureNeedsUpdate = true;
    PushUndo("Apply palette");
}

// Floyd-Steinberg dithering
void PixelPaintView::ApplyFloydSteinbergDithering(const std::vector<pelpaint::Pixel>& palette)
{
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer) return;

    std::vector<pelpaint::Pixel> tempData = activeLayer->pixelData;

    for (int y = 0; y < canvasHeight; ++y) {
        for (int x = 0; x < canvasWidth; ++x) {
            pelpaint::Pixel oldPixel = tempData[GetPixelIndex(x, y)];
            pelpaint::Pixel newPixel = FindNearestPaletteColor(oldPixel, palette);

            if (ditheringPreserveAlpha) {
                newPixel.a = oldPixel.a;
            }

            tempData[GetPixelIndex(x, y)] = newPixel;

            int errorR = static_cast<int>(oldPixel.r) - static_cast<int>(newPixel.r);
            int errorG = static_cast<int>(oldPixel.g) - static_cast<int>(newPixel.g);
            int errorB = static_cast<int>(oldPixel.b) - static_cast<int>(newPixel.b);

            if (x + 1 < canvasWidth) {
                pelpaint::Pixel& neighbor = tempData[GetPixelIndex(x + 1, y)];
                neighbor.r = std::clamp(static_cast<int>(neighbor.r) + errorR * 7 / 16, 0, 255);
                neighbor.g = std::clamp(static_cast<int>(neighbor.g) + errorG * 7 / 16, 0, 255);
                neighbor.b = std::clamp(static_cast<int>(neighbor.b) + errorB * 7 / 16, 0, 255);
            }

            if (y + 1 < canvasHeight) {
                if (x - 1 >= 0) {
                    pelpaint::Pixel& neighbor = tempData[GetPixelIndex(x - 1, y + 1)];
                    neighbor.r = std::clamp(static_cast<int>(neighbor.r) + errorR * 3 / 16, 0, 255);
                    neighbor.g = std::clamp(static_cast<int>(neighbor.g) + errorG * 3 / 16, 0, 255);
                    neighbor.b = std::clamp(static_cast<int>(neighbor.b) + errorB * 3 / 16, 0, 255);
                }

                pelpaint::Pixel& neighbor = tempData[GetPixelIndex(x, y + 1)];
                neighbor.r = std::clamp(static_cast<int>(neighbor.r) + errorR * 5 / 16, 0, 255);
                neighbor.g = std::clamp(static_cast<int>(neighbor.g) + errorG * 5 / 16, 0, 255);
                neighbor.b = std::clamp(static_cast<int>(neighbor.b) + errorB * 5 / 16, 0, 255);

                if (x + 1 < canvasWidth) {
                    pelpaint::Pixel& neighbor = tempData[GetPixelIndex(x + 1, y + 1)];
                    neighbor.r = std::clamp(static_cast<int>(neighbor.r) + errorR * 1 / 16, 0, 255);
                    neighbor.g = std::clamp(static_cast<int>(neighbor.g) + errorG * 1 / 16, 0, 255);
                    neighbor.b = std::clamp(static_cast<int>(neighbor.b) + errorB * 1 / 16, 0, 255);
                }
            }
        }
    }

    activeLayer->pixelData = tempData;
    RenderLayerToCanvas();
    textureNeedsUpdate = true;
    PushUndo("Apply dithering");
}



void PixelPaintView::AddCheckbox(const std::string& label, const std::function<void(bool)>& callback)
{
    // Persistent checkbox backed by `checkboxValues[label]`.
    // Initialize to false when first created.
    auto it = checkboxValues.find(label);
    if (it == checkboxValues.end()) {
        it = checkboxValues.emplace(label, false).first;
    }

    bool value = it->second;
    if (ImGui::Checkbox(label.c_str(), &value)) {
        // User toggled the checkbox
        it->second = value;
        if (callback) callback(value);
    } else {
        // Keep stored value in sync
        it->second = value;
    }
}

void PixelPaintView::AddButton(const std::string& label, const std::function<void()>& callback)
{
    if (ImGui::Button(label.c_str())) {
        callback();
    }
}

void PixelPaintView::ConvertToGrayscale()
{
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer) return;

    // Convert active layer to grayscale
    for (auto& pixel : activeLayer->pixelData) {
        int gray = static_cast<int>(0.299 * pixel.r + 0.587 * pixel.g + 0.114 * pixel.b);
        pixel.r = pixel.g = pixel.b = gray;
    }
    RenderLayerToCanvas();
    textureNeedsUpdate = true;
    PushUndo("To Grayscale");
    textureNeedsUpdate = true;
    PushUndo("Convert to grayscale");
}


void PixelPaintView::ApplyAtkinsonDithering(const std::vector<pelpaint::Pixel>& palette)
{
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer) return;

    // Iterate over each pixel in the canvas
    for (int y = 0; y < canvasHeight; ++y) {
        for (int x = 0; x < canvasWidth; ++x) {
            pelpaint::Pixel& currentPixel = activeLayer->pixelData[GetPixelIndex(x, y)];
            pelpaint::Pixel closestColor = FindNearestPaletteColor(currentPixel, palette);

            // Calculate the error
            int errorR = currentPixel.r - closestColor.r;
            int errorG = currentPixel.g - closestColor.g;
            int errorB = currentPixel.b - closestColor.b;

            // Apply the closest color
            currentPixel = closestColor;

            // Diffuse the error to neighboring pixels
            DiffuseError(x, y, errorR, errorG, errorB, 1, 1, 1, 8); // Atkinson pattern
        }
    }
    RenderLayerToCanvas();
    textureNeedsUpdate = true;
}



void PixelPaintView::ApplyStuckiDithering(const std::vector<pelpaint::Pixel>& palette)
{
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer) return;

    // Iterate over each pixel in the canvas
    for (int y = 0; y < canvasHeight; ++y) {
        for (int x = 0; x < canvasWidth; ++x) {
            pelpaint::Pixel& currentPixel = activeLayer->pixelData[GetPixelIndex(x, y)];
            pelpaint::Pixel closestColor = FindNearestPaletteColor(currentPixel, palette);

            // Calculate the error
            int errorR = currentPixel.r - closestColor.r;
            int errorG = currentPixel.g - closestColor.g;
            int errorB = currentPixel.b - closestColor.b;

            // Apply the closest color
            currentPixel = closestColor;

            // Diffuse the error to neighboring pixels
            DiffuseError(x, y, errorR, errorG, errorB, 2, 2, 2, 42); // Stucki pattern
        }
    }
    RenderLayerToCanvas();
    textureNeedsUpdate = true;
}



// Ordered dithering
void PixelPaintView::ApplyOrderedDithering(const std::vector<pelpaint::Pixel>& palette)
{
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer) return;

    const int ditherPatternSize = 4;
    const int ditherPattern[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5}
    };

    for (int y = 0; y < canvasHeight; ++y) {
        for (int x = 0; x < canvasWidth; ++x) {
            pelpaint::Pixel pixel = activeLayer->pixelData[GetPixelIndex(x, y)];
            int ditherValue = ditherPattern[y % ditherPatternSize][x % ditherPatternSize];

            pelpaint::Pixel ditheredPixel;
            ditheredPixel.r = std::clamp(static_cast<int>(pixel.r) + ditherValue - 8, 0, 255);
            ditheredPixel.g = std::clamp(static_cast<int>(pixel.g) + ditherValue - 8, 0, 255);
            ditheredPixel.b = std::clamp(static_cast<int>(pixel.b) + ditherValue - 8, 0, 255);
            ditheredPixel.a = pixel.a;

            pelpaint::Pixel quantized = FindNearestPaletteColor(ditheredPixel, palette);

            if (ditheringPreserveAlpha) {
                quantized.a = pixel.a;
            }

            activeLayer->pixelData[GetPixelIndex(x, y)] = quantized;
        }
    }

    RenderLayerToCanvas();
    textureNeedsUpdate = true;
    PushUndo("Apply ordered dithering");

}

// Pixelify - Create pixel art effect by averaging blocks and optionally quantizing to palette
void PixelPaintView::ApplyPixelify(int pixelSize, bool usePalette)
{
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer || pixelSize < 1) return;

    // Create output buffer
    std::vector<pelpaint::Pixel> result = activeLayer->pixelData;

    // Get the palette to use for quantization
    const std::vector<pelpaint::Pixel>& palette = customPalette.empty() ? availablePalettes[selectedPaletteIndex].colors : customPalette;

    // Process the image in blocks
    for (int blockY = 0; blockY < canvasHeight; blockY += pixelSize) {
        for (int blockX = 0; blockX < canvasWidth; blockX += pixelSize) {
            // Calculate block bounds
            int maxX = std::min(blockX + pixelSize, canvasWidth);
            int maxY = std::min(blockY + pixelSize, canvasHeight);

            // Average the colors in this block
            int sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int pixelCount = 0;

            for (int y = blockY; y < maxY; ++y) {
                for (int x = blockX; x < maxX; ++x) {
                    pelpaint::Pixel p = activeLayer->pixelData[GetPixelIndex(x, y)];
                    sumR += p.r;
                    sumG += p.g;
                    sumB += p.b;
                    sumA += p.a;
                    pixelCount++;
                }
            }

            // Calculate average color
            pelpaint::Pixel averageColor;
            averageColor.r = static_cast<uint8_t>(sumR / pixelCount);
            averageColor.g = static_cast<uint8_t>(sumG / pixelCount);
            averageColor.b = static_cast<uint8_t>(sumB / pixelCount);
            averageColor.a = static_cast<uint8_t>(sumA / pixelCount);

            // Apply palette quantization if enabled
            if (usePalette && !palette.empty()) {
                averageColor = FindNearestPaletteColor(averageColor, palette);
            }

            // Fill the block with the averaged (and possibly quantized) color
            for (int y = blockY; y < maxY; ++y) {
                for (int x = blockX; x < maxX; ++x) {
                    result[GetPixelIndex(x, y)] = averageColor;
                }
            }
        }
    }

    // Apply the result to the active layer
    activeLayer->pixelData = result;
    RenderLayerToCanvas();
    textureNeedsUpdate = true;
    PushUndo("Pixelify");
}

// Calculate automatic pixel size based on image dimensions
int PixelPaintView::CalculateAutoPixelSize(int imageWidth, int imageHeight) const
{
    // Aim for roughly 256-512 visible pixels (reasonable for editing)
    int maxDim = std::max(imageWidth, imageHeight);

    if (maxDim <= 256) return 1;      // Tiny image, no pixelify needed
    if (maxDim <= 512) return 2;      // Small image
    if (maxDim <= 800) return 3;      // Medium image
    if (maxDim <= 1200) return 4;     // Medium-large image
    if (maxDim <= 1600) return 6;     // Large image
    if (maxDim <= 2400) return 8;     // Very large image
    return 10;                         // Massive image
}

// Smart Repaint - Get shaded color based on background brightness
pelpaint::Pixel PixelPaintView::GetShadedColor(const pelpaint::Pixel& baseColor, const pelpaint::Pixel& bgColor, bool darker)
{
    pelpaint::Pixel result = baseColor;

    // Calculate brightness of background color
    float bgBrightness = (bgColor.r + bgColor.g + bgColor.b) / (3.0f * 255.0f);

    if (darker) {
        // Make color darker
        float factor = bgBrightness > 0.5f ? 0.7f : 0.5f;
        result.r = static_cast<uint8_t>(result.r * factor);
        result.g = static_cast<uint8_t>(result.g * factor);
        result.b = static_cast<uint8_t>(result.b * factor);
    } else {
        // Make color lighter
        float factor = bgBrightness > 0.5f ? 1.2f : 1.5f;
        result.r = std::min(255, static_cast<int>(result.r * factor));
        result.g = std::min(255, static_cast<int>(result.g * factor));
        result.b = std::min(255, static_cast<int>(result.b * factor));
    }

    result.a = baseColor.a;
    return result;
}

// Smart Repaint - Check if color is light or dark
bool PixelPaintView::IsColorLight(const pelpaint::Pixel& color) const
{
    float brightness = (color.r + color.g + color.b) / (3.0f * 255.0f);
    return brightness > 0.5f;
}

// Smart Repaint - Draw shape with intelligent shading
void PixelPaintView::DrawShapeRedrawShape(int cx, int cy, const pelpaint::Pixel& fgColor, const pelpaint::Pixel& bgColor, ShapeRedrawShape shape, int size)
{
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer) return;

    pelpaint::Pixel lightColor = GetShadedColor(fgColor, bgColor, false);
    pelpaint::Pixel darkColor = GetShadedColor(fgColor, bgColor, true);

    switch (shape) {
        case ShapeRedrawShape::Dot: {
            PutPixel(cx, cy, fgColor);
            break;
        }

        case ShapeRedrawShape::Circle: {
            int radius = std::max(1, size / 2);
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dy * dy <= radius * radius) {
                        int x = cx + dx;
                        int y = cy + dy;
                        bool isEdge = (dx * dx + dy * dy > (radius - 1) * (radius - 1));
                        pelpaint::Pixel color = isEdge ? darkColor : fgColor;
                        PutPixel(x, y, color);
                    }
                }
            }
            break;
        }

        case ShapeRedrawShape::Rectangle: {
            int halfSize = std::max(1, size / 2);
            for (int dy = -halfSize; dy <= halfSize; ++dy) {
                for (int dx = -halfSize; dx <= halfSize; ++dx) {
                    int x = cx + dx;
                    int y = cy + dy;
                    bool isEdge = (std::abs(dx) == halfSize || std::abs(dy) == halfSize);
                    pelpaint::Pixel color = isEdge ? darkColor : fgColor;
                    PutPixel(x, y, color);
                }
            }
            break;
        }

        case ShapeRedrawShape::RectOutline: {
            int halfSize = std::max(1, size / 2);
            for (int dx = -halfSize; dx <= halfSize; ++dx) {
                PutPixel(cx + dx, cy - halfSize, darkColor);
                PutPixel(cx + dx, cy + halfSize, darkColor);
            }
            for (int dy = -halfSize; dy <= halfSize; ++dy) {
                PutPixel(cx - halfSize, cy + dy, darkColor);
                PutPixel(cx + halfSize, cy + dy, darkColor);
            }
            break;
        }

        case ShapeRedrawShape::Gem:
        case ShapeRedrawShape::Diamond: {
            int halfSize = std::max(1, size / 2);
            for (int dy = 0; dy <= halfSize; ++dy) {
                int width = (dy * (halfSize + 1)) / halfSize;
                for (int dx = -width; dx <= width; ++dx) {
                    PutPixel(cx + dx, cy - halfSize + dy, lightColor);
                }
            }
            for (int dy = 0; dy <= halfSize; ++dy) {
                int width = ((halfSize - dy) * (halfSize + 1)) / halfSize;
                for (int dx = -width; dx <= width; ++dx) {
                    PutPixel(cx + dx, cy + dy, darkColor);
                }
            }
            break;
        }
    }

    RenderLayerToCanvas();
    textureNeedsUpdate = true;
}

// -----------------------------------------------------------------------
// Shape Redraw Filter - pixelization-style effect using Square, Dot, or
// Custom 8x8 shape stamp per block.
// -----------------------------------------------------------------------
void PixelPaintView::ApplyShapeRedrawFilter()
{
    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer) return;

    const int blockSize = std::max(1, shapeRedrawFilterBlockSize);
    const int padding   = std::max(0, shapeRedrawFilterPadding);
    const std::vector<pelpaint::Pixel>& palette =
        customPalette.empty() ? availablePalettes[selectedPaletteIndex].colors : customPalette;

    // Determine background pixel
    pelpaint::Pixel bgPixel;
    switch (shapeRedrawBgMode) {
        case ShapeRedrawBgMode::Black: bgPixel = pelpaint::Pixel(0,   0,   0,   255); break;
        case ShapeRedrawBgMode::White: bgPixel = pelpaint::Pixel(255, 255, 255, 255); break;
        case ShapeRedrawBgMode::Alpha: bgPixel = pelpaint::Pixel(0,   0,   0,   0);   break;
    }

    // Work on a fresh output buffer, initialized to the background color
    std::vector<pelpaint::Pixel> result(canvasWidth * canvasHeight, bgPixel);

    for (int blockY = 0; blockY < canvasHeight; blockY += blockSize) {
        for (int blockX = 0; blockX < canvasWidth; blockX += blockSize) {
            int maxX = std::min(blockX + blockSize, canvasWidth);
            int maxY = std::min(blockY + blockSize, canvasHeight);

            // Average color of this block
            int sumR = 0, sumG = 0, sumB = 0, sumA = 0, count = 0;
            for (int y = blockY; y < maxY; ++y) {
                for (int x = blockX; x < maxX; ++x) {
                    pelpaint::Pixel p = activeLayer->pixelData[GetPixelIndex(x, y)];
                    sumR += p.r; sumG += p.g; sumB += p.b; sumA += p.a;
                    ++count;
                }
            }
            if (count == 0) continue;

            pelpaint::Pixel avgColor;
            avgColor.r = static_cast<uint8_t>(sumR / count);
            avgColor.g = static_cast<uint8_t>(sumG / count);
            avgColor.b = static_cast<uint8_t>(sumB / count);
            avgColor.a = static_cast<uint8_t>(sumA / count);

            if (shapeRedrawFilterUsePalette && !palette.empty()) {
                avgColor = FindNearestPaletteColor(avgColor, palette);
            }

            // Inner drawable area: block minus padding on all sides
            int innerX0 = blockX + padding;
            int innerY0 = blockY + padding;
            int innerX1 = maxX  - padding;
            int innerY1 = maxY  - padding;
            if (innerX1 <= innerX0 || innerY1 <= innerY0) continue;

            int innerW = innerX1 - innerX0;
            int innerH = innerY1 - innerY0;
            int cx = innerX0 + innerW / 2;
            int cy = innerY0 + innerH / 2;
            int radius = std::min(innerW, innerH) / 2;

            switch (shapeRedrawFilterMode) {
                case ShapeRedrawFilterMode::Square: {
                    for (int y = innerY0; y < innerY1; ++y) {
                        for (int x = innerX0; x < innerX1; ++x) {
                            if (x >= 0 && x < canvasWidth && y >= 0 && y < canvasHeight)
                                result[GetPixelIndex(x, y)] = avgColor;
                        }
                    }
                    break;
                }
                case ShapeRedrawFilterMode::Dot: {
                    for (int dy = -radius; dy <= radius; ++dy) {
                        for (int dx = -radius; dx <= radius; ++dx) {
                            if (dx * dx + dy * dy <= radius * radius) {
                                int px = cx + dx, py = cy + dy;
                                if (px >= 0 && px < canvasWidth && py >= 0 && py < canvasHeight)
                                    result[GetPixelIndex(px, py)] = avgColor;
                            }
                        }
                    }
                    break;
                }
                case ShapeRedrawFilterMode::Custom: {
                    // Stamp the 8x8 custom map, scaled to fit innerW x innerH
                    for (int row = 0; row < 8; ++row) {
                        for (int col = 0; col < 8; ++col) {
                            if (!shapeRedrawCustomMap[row * 8 + col]) continue;
                            int px = innerX0 + (col * innerW) / 8;
                            int py = innerY0 + (row * innerH) / 8;
                            int pw = std::max(1, innerW / 8);
                            int ph = std::max(1, innerH / 8);
                            for (int oy = 0; oy < ph; ++oy) {
                                for (int ox = 0; ox < pw; ++ox) {
                                    int fx = px + ox, fy = py + oy;
                                    if (fx >= 0 && fx < canvasWidth && fy >= 0 && fy < canvasHeight)
                                        result[GetPixelIndex(fx, fy)] = avgColor;
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }
    }

    activeLayer->pixelData = result;
    RenderLayerToCanvas();
    textureNeedsUpdate = true;
    PushUndo("Shape Redraw Filter");
}

// Undo/Redo
void PixelPaintView::PushUndo(const std::string& description)
{
    if (undoStack.size() >= maxUndoSteps) {
        undoStack.erase(undoStack.begin());
    }
    undoStack.emplace_back(layers, activeLayerIndex, canvasWidth, canvasHeight, description);
    redoStack.clear();
}

void PixelPaintView::Undo()
{
    if (undoStack.size() <= 1) return;

    // Save current state to redo stack
    redoStack.emplace_back(layers, activeLayerIndex, canvasWidth, canvasHeight, undoStack.back().description);
    undoStack.pop_back();

    // Restore from undo stack
    const auto& snapshot = undoStack.back();
    layers = snapshot.layers;
    activeLayerIndex = snapshot.activeLayerIndex;
    canvasWidth = snapshot.canvasWidth;
    canvasHeight = snapshot.canvasHeight;

    RenderLayerToCanvas();
    textureNeedsUpdate = true;
}

void PixelPaintView::Redo()
{
    if (redoStack.empty()) return;

    // Save current state to undo stack
    undoStack.emplace_back(layers, activeLayerIndex, canvasWidth, canvasHeight, redoStack.back().description);

    // Restore from redo stack
    const auto& snapshot = redoStack.back();
    layers = snapshot.layers;
    activeLayerIndex = snapshot.activeLayerIndex;
    canvasWidth = snapshot.canvasWidth;
    canvasHeight = snapshot.canvasHeight;

    redoStack.pop_back();
    RenderLayerToCanvas();
    textureNeedsUpdate = true;
}

void PixelPaintView::ClearUndoStack()
{
    undoStack.clear();
    redoStack.clear();
}

// File I/O
bool PixelPaintView::SaveToTGA(const std::string& filename)
{
    std::vector<pelpaint::Pixel> composite;
    CompositeLayers(composite);

    FILE* file = fopen(filename.c_str(), "wb");
    if (!file) return false;

    uint8_t header[18] = {0};
    header[2] = 2;
    header[12] = canvasWidth & 0xFF;
    header[13] = (canvasWidth >> 8) & 0xFF;
    header[14] = canvasHeight & 0xFF;
    header[15] = (canvasHeight >> 8) & 0xFF;
    header[16] = 32;
    header[17] = 0x20;

    fwrite(header, 1, 18, file);

    for (const auto& pixel : composite) {
        fputc(pixel.b, file);
        fputc(pixel.g, file);
        fputc(pixel.r, file);
        fputc(pixel.a, file);
    }

    fclose(file);

    // Save directory for next file dialog
    fs::path p(filename);
    SaveLastDirectory(p.parent_path().string());

    return true;
}

bool PixelPaintView::SaveToPNG(const std::string& filename)
{
    std::vector<pelpaint::Pixel> composite;
    CompositeLayers(composite);

    std::vector<uint8_t> pngData;
    pngData.reserve(canvasWidth * canvasHeight * 4);

    for (const auto& pixel : composite) {
        pngData.push_back(pixel.r);
        pngData.push_back(pixel.g);
        pngData.push_back(pixel.b);
        pngData.push_back(pixel.a);
    }

    bool success = stbi_write_png(filename.c_str(), canvasWidth, canvasHeight, 4, pngData.data(), canvasWidth * 4);

    // Save directory for next file dialog
    if (success) {
        fs::path p(filename);
        SaveLastDirectory(p.parent_path().string());
    }

    return success;
}

bool PixelPaintView::SaveToSVGPixel(const std::string& filename)
{
    std::vector<pelpaint::Pixel> composite;
    CompositeLayers(composite);

    ImageView view;
    view.data = reinterpret_cast<const std::uint8_t*>(composite.data());
    view.width = static_cast<std::uint32_t>(canvasWidth);
    view.height = static_cast<std::uint32_t>(canvasHeight);
    view.stride = view.width * 4;
    view.channels = 4;

    // Implementation uses optimized greedy rectangle merging
    bool success = ImageExporter::SaveToSVGOptimized(filename, view);
    if (success) {
        fs::path p(filename);
        SaveLastDirectory(p.parent_path().string());
    }
    return success;
}

bool PixelPaintView::SaveToSVGVector(const std::string& filename)
{
    std::vector<pelpaint::Pixel> composite;
    CompositeLayers(composite);

    ImageView view;
    view.data = reinterpret_cast<const std::uint8_t*>(composite.data());
    view.width = static_cast<std::uint32_t>(canvasWidth);
    view.height = static_cast<std::uint32_t>(canvasHeight);
    view.stride = view.width * 4;
    view.channels = 4;

    // Implementation uses optimized greedy rectangle merging with vector styling
    bool success = ImageExporter::SaveToSVGVector(filename, view);
    if (success) {
        fs::path p(filename);
        SaveLastDirectory(p.parent_path().string());
    }
    return success;
}

bool PixelPaintView::SaveDepthMap(const std::string& filename)
{
    if (depthMapGridSize < 1) depthMapGridSize = 1;

    std::vector<pelpaint::Pixel> composite;
    CompositeLayers(composite);

    ImageView view;
    view.data = reinterpret_cast<const std::uint8_t*>(composite.data());
    view.width = static_cast<std::uint32_t>(canvasWidth);
    view.height = static_cast<std::uint32_t>(canvasHeight);
    view.stride = view.width * 4;
    view.channels = 4;

    bool success = ImageExporter::SaveDepthMap(
        view,
        static_cast<std::uint32_t>(depthMapGridSize),
        filename
    );

    if (success) {
        fs::path p(filename);
        SaveLastDirectory(p.parent_path().string());
    }

    return success;
}

bool PixelPaintView::SaveMesh(const std::string& filename)
{
    if (meshExportGridSize < 1) meshExportGridSize = 1;

    std::vector<pelpaint::Pixel> composite;
    CompositeLayers(composite);

    ImageView view;
    view.data = reinterpret_cast<const std::uint8_t*>(composite.data());
    view.width = static_cast<std::uint32_t>(canvasWidth);
    view.height = static_cast<std::uint32_t>(canvasHeight);
    view.stride = view.width * 4;
    view.channels = 4;

    MeshExportOptions options;
    options.mode = static_cast<MeshMode>(meshExportMode);
    options.gridSize = static_cast<std::uint32_t>(meshExportGridSize);
    options.depthScale = 1.0f;
    options.useVertexColors = true;
    options.optimizeMesh = true;

    pelpaint::ColorPalette fallback("Default", {});
    const bool paletteOk = selectedPaletteIndex >= 0 &&
                           selectedPaletteIndex < static_cast<int>(availablePalettes.size());
    const pelpaint::ColorPalette& palette = paletteOk ? availablePalettes[selectedPaletteIndex] : fallback;

    bool success = MeshExporter::SaveAsMesh(filename, view, palette, options);
    if (success) {
        fs::path p(filename);
        SaveLastDirectory(p.parent_path().string());
    }

    return success;
}

bool PixelPaintView::SaveToJPEG(const std::string& filename, int quality)
{
    std::vector<pelpaint::Pixel> composite;
    CompositeLayers(composite);

    std::vector<uint8_t> jpegData;
    jpegData.reserve(canvasWidth * canvasHeight * 3);

    for (const auto& pixel : composite) {
        jpegData.push_back(pixel.r);
        jpegData.push_back(pixel.g);
        jpegData.push_back(pixel.b);
    }

    bool success = stbi_write_jpg(filename.c_str(), canvasWidth, canvasHeight, 3, jpegData.data(), quality);

    // Save directory for next file dialog
    if (success) {
        fs::path p(filename);
        SaveLastDirectory(p.parent_path().string());
    }

    return success;
}

bool PixelPaintView::LoadFromImage(const std::string& filename)
{
    int width, height, channels;
    unsigned char* imageData = stbi_load(filename.c_str(), &width, &height, &channels, 4);

    if (!imageData) return false;

    ResizeCanvas(width, height);

    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer) {
        stbi_image_free(imageData);
        return false;
    }

    for (int i = 0; i < width * height; ++i) {
        activeLayer->pixelData[i].r = imageData[i * 4];
        activeLayer->pixelData[i].g = imageData[i * 4 + 1];
        activeLayer->pixelData[i].b = imageData[i * 4 + 2];
        activeLayer->pixelData[i].a = imageData[i * 4 + 3];
    }

    stbi_image_free(imageData);
    RenderLayerToCanvas();
    textureNeedsUpdate = true;
    SetFilenameFromLoadedImage(filename);
    PushUndo("Load image");

    // Auto-pixelify large images to prevent memory/performance issues
    if (autoPixelifyOnLoad && width > autoPixelifyThreshold) {
        int calculatedPixelSize = CalculateAutoPixelSize(width, height);

        // Ensure palette is selected
        if (selectedPaletteIndex < 0 || selectedPaletteIndex >= availablePalettes.size()) {
            selectedPaletteIndex = 0;
        }
        paletteEnabled = true;

        // Apply pixelify automatically
        ApplyPixelify(calculatedPixelSize, true);
    }

    return true;
}

bool PixelPaintView::IsRectSelectionActive() const
{
    return currentSelection.isActive && currentSelection.type == SelectionData::Rectangle;
}

bool PixelPaintView::IsPointInSelection(int x, int y) const
{
    if (!currentSelection.isActive) return true;

    switch (currentSelection.type) {
        case SelectionData::Rectangle: {
            const int x1 = static_cast<int>(std::floor(std::min(currentSelection.selectionStart.x, currentSelection.selectionEnd.x)));
            const int y1 = static_cast<int>(std::floor(std::min(currentSelection.selectionStart.y, currentSelection.selectionEnd.y)));
            const int x2 = static_cast<int>(std::floor(std::max(currentSelection.selectionStart.x, currentSelection.selectionEnd.x)));
            const int y2 = static_cast<int>(std::floor(std::max(currentSelection.selectionStart.y, currentSelection.selectionEnd.y)));
            return x >= x1 && x <= x2 && y >= y1 && y <= y2;
        }
        case SelectionData::Circle: {
            const int x1 = static_cast<int>(std::min(currentSelection.selectionStart.x, currentSelection.selectionEnd.x));
            const int y1 = static_cast<int>(std::min(currentSelection.selectionStart.y, currentSelection.selectionEnd.y));
            const int x2 = static_cast<int>(std::max(currentSelection.selectionStart.x, currentSelection.selectionEnd.x));
            const int y2 = static_cast<int>(std::max(currentSelection.selectionStart.y, currentSelection.selectionEnd.y));
            const int centerX = (x1 + x2) / 2;
            const int centerY = (y1 + y2) / 2;
            const int radiusX = (x2 - x1) / 2;
            const int radiusY = (y2 - y1) / 2;
            if (radiusX <= 0 || radiusY <= 0) return false;
            const int dx = x - centerX;
            const int dy = y - centerY;
            return (dx * dx) / (radiusX * radiusX + 1) + (dy * dy) / (radiusY * radiusY + 1) <= 1;
        }
        case SelectionData::Polygon: {
            ImVec2 p(static_cast<float>(x), static_cast<float>(y));
            return IsPointInPolygon(p, currentSelection.polygonPoints);
        }
        default:
            return true;
    }
}

void PixelPaintView::CropToSelection()
{
    if (!IsRectSelectionActive()) return;

    int x1 = static_cast<int>(std::floor(std::min(currentSelection.selectionStart.x, currentSelection.selectionEnd.x)));
    int y1 = static_cast<int>(std::floor(std::min(currentSelection.selectionStart.y, currentSelection.selectionEnd.y)));
    int x2 = static_cast<int>(std::floor(std::max(currentSelection.selectionStart.x, currentSelection.selectionEnd.x)));
    int y2 = static_cast<int>(std::floor(std::max(currentSelection.selectionStart.y, currentSelection.selectionEnd.y)));

    // Clamp to canvas
    x1 = std::max(0, std::min(x1, canvasWidth - 1));
    y1 = std::max(0, std::min(y1, canvasHeight - 1));
    x2 = std::max(0, std::min(x2, canvasWidth - 1));
    y2 = std::max(0, std::min(y2, canvasHeight - 1));

    int newWidth = x2 - x1 + 1;
    int newHeight = y2 - y1 + 1;

    if (newWidth <= 0 || newHeight <= 0) return;

    PushUndo("Crop to Selection");

    // Extract cropped data for each layer
    for (auto& layer : layers) {
        std::vector<pelpaint::Pixel> croppedData(newWidth * newHeight);
        for (int y = 0; y < newHeight; ++y) {
            for (int x = 0; x < newWidth; ++x) {
                croppedData[y * newWidth + x] = layer.pixelData[(y1 + y) * canvasWidth + (x1 + x)];
            }
        }
        layer.pixelData = std::move(croppedData);
    }

    canvasWidth = newWidth;
    canvasHeight = newHeight;
    canvasData.assign(canvasWidth * canvasHeight, pelpaint::Pixel(0, 0, 0, 0));

    currentSelection.isActive = false;
    RenderLayerToCanvas();
    textureNeedsUpdate = true;
}

std::string PixelPaintView::GetDefaultFilename(const std::string& extension)
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto tm = std::localtime(&time);

    std::ostringstream oss;
    oss << "artwork_"
        << std::put_time(tm, "%Y%m%d_%H%M%S")
        << "." << extension;

    return oss.str();
}

void PixelPaintView::SetFilenameFromLoadedImage(const std::string& imagePath)
{
    // Extract filename from path and remove extension
    fs::path p(imagePath);
    std::string stem = p.stem().string();

    if (!stem.empty()) {
        currentFilename = stem;
    } else {
        currentFilename = GetDefaultFilename("png");
    }

    // Save the directory for next file dialog
    SaveLastDirectory(p.parent_path().string());
}

// Get home directory cross-platform
std::string PixelPaintView::GetHomeDirectory() const
{
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
    if (home) return home;
    return ".";
#else
    const char* home = std::getenv("HOME");
    if (home) return home;
    return ".";
#endif
}

// Load last used directory from config file
void PixelPaintView::LoadLastDirectory()
{
    std::string configDir = GetHomeDirectory();
#ifdef _WIN32
    std::string configPath = configDir + "\\.pelpaint";
#else
    std::string configPath = configDir + "/.pelpaint";
#endif

    // Create directory if it doesn't exist
    try {
        fs::create_directories(configPath);
    } catch (const std::exception& e) {
        // Silently fail if we can't create directory
        lastDirectory = GetHomeDirectory();
        return;
    }

#ifdef _WIN32
    std::string filePath = configPath + "\\last_dir.txt";
#else
    std::string filePath = configPath + "/last_dir.txt";
#endif

    std::ifstream file(filePath);
    if (file.is_open()) {
        std::string line;
        if (std::getline(file, line) && !line.empty()) {
            // Verify directory exists
            if (fs::exists(line) && fs::is_directory(line)) {
                lastDirectory = line;
            } else {
                lastDirectory = GetHomeDirectory();
            }
        } else {
            lastDirectory = GetHomeDirectory();
        }
        file.close();
    } else {
        lastDirectory = GetHomeDirectory();
    }
}

// Save last used directory to config file
void PixelPaintView::SaveLastDirectory(const std::string& directory)
{
    if (directory.empty() || directory == ".") return;

    // Verify directory exists
    if (!fs::exists(directory) || !fs::is_directory(directory)) return;

    lastDirectory = directory;

    std::string configDir = GetHomeDirectory();
#ifdef _WIN32
    std::string configPath = configDir + "\\.pelpaint";
#else
    std::string configPath = configDir + "/.pelpaint";
#endif

    // Create directory if it doesn't exist
    try {
        fs::create_directories(configPath);
    } catch (const std::exception& e) {
        // Silently fail if we can't create directory
        return;
    }

#ifdef _WIN32
    std::string filePath = configPath + "\\last_dir.txt";
#else
    std::string filePath = configPath + "/last_dir.txt";
#endif

    std::ofstream file(filePath);
    if (file.is_open()) {
        file << directory;
        file.close();
    }
}

bool PixelPaintView::SaveBinary(const std::string& filename)
{
    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;

    uint32_t width = canvasWidth;
    uint32_t height = canvasHeight;
    file.write(reinterpret_cast<char*>(&width), sizeof(width));
    file.write(reinterpret_cast<char*>(&height), sizeof(height));

    for (const auto& pixel : canvasData) {
        file.write(reinterpret_cast<const char*>(&pixel), sizeof(pixel));
    }

    file.close();

    // Save directory for next file dialog
    fs::path p(filename);
    SaveLastDirectory(p.parent_path().string());

    return true;
}

bool PixelPaintView::LoadBinary(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;

    uint32_t width, height;
    file.read(reinterpret_cast<char*>(&width), sizeof(width));
    file.read(reinterpret_cast<char*>(&height), sizeof(height));

    ResizeCanvas(width, height);

    for (auto& pixel : canvasData) {
        file.read(reinterpret_cast<char*>(&pixel), sizeof(pixel));
    }

    file.close();

    // Save directory for next file dialog
    fs::path p(filename);
    SaveLastDirectory(p.parent_path().string());

    textureNeedsUpdate = true;
    PushUndo("Load binary");
    return true;
}

// Utility functions
ImVec2 PixelPaintView::ScreenToCanvas(const ImVec2& screenPos) const
{
    ImVec2 relPos = ImVec2(screenPos.x - canvasPos.x, screenPos.y - canvasPos.y);
    return ImVec2(relPos.x / canvasScale + scrollOffset.x, relPos.y / canvasScale + scrollOffset.y);
}

ImVec2 PixelPaintView::CanvasToScreen(const ImVec2& canvasPos) const
{
    return ImVec2(
        this->canvasPos.x + (canvasPos.x - scrollOffset.x) * canvasScale,
        this->canvasPos.y + (canvasPos.y - scrollOffset.y) * canvasScale
    );
}

// Update frequent colors
void PixelPaintView::UpdateFrequentColors()
{
    std::map<uint32_t, int> colorFrequency;

    for (const auto& pixel : canvasData) {
        uint32_t colorKey = (pixel.r << 24) | (pixel.g << 16) | (pixel.b << 8) | pixel.a;
        colorFrequency[colorKey]++;
    }

    std::vector<std::pair<uint32_t, int>> sortedColors(colorFrequency.begin(), colorFrequency.end());
    std::sort(sortedColors.begin(), sortedColors.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    frequentColors.clear();
    for (size_t i = 0; i < sortedColors.size() && i < maxMostUsedColors; ++i) {
        uint32_t key = sortedColors[i].first;
        frequentColors.push_back(pelpaint::Pixel(
            (key >> 24) & 0xFF,
            (key >> 16) & 0xFF,
            (key >> 8) & 0xFF,
            key & 0xFF
        ));
    }
}

// Copy selection
void PixelPaintView::CopySelection(const ImVec2& startPoint, const ImVec2& endPoint, bool isCircle)
{
    int x1 = static_cast<int>(std::min(startPoint.x, endPoint.x));
    int y1 = static_cast<int>(std::min(startPoint.y, endPoint.y));
    int x2 = static_cast<int>(std::max(startPoint.x, endPoint.x));
    int y2 = static_cast<int>(std::max(startPoint.y, endPoint.y));

    currentSelection.type = isCircle ? SelectionData::Circle : SelectionData::Rectangle;
    currentSelection.canBlur = true;

    currentSelection.width = x2 - x1 + 1;
    currentSelection.height = y2 - y1 + 1;
    currentSelection.selectionStart = ImVec2(static_cast<float>(x1), static_cast<float>(y1));
    currentSelection.selectionEnd = ImVec2(static_cast<float>(x2), static_cast<float>(y2));
    currentSelection.sourceCenter = ImVec2(
        (x1 + x2) / 2.0f,
        (y1 + y2) / 2.0f
    );

    currentSelection.pixels.clear();
    currentSelection.pixels.resize(
        static_cast<std::size_t>(currentSelection.width) * currentSelection.height,
        pelpaint::Pixel(0, 0, 0, 0)
    );

    int centerX = (x1 + x2) / 2;
    int centerY = (y1 + y2) / 2;
    int radiusX = (x2 - x1) / 2;
    int radiusY = (y2 - y1) / 2;

    for (int y = y1; y <= y2; ++y) {
        for (int x = x1; x <= x2; ++x) {
            bool inSelection = isCircle ?
                ((x - centerX) * (x - centerX) / (radiusX * radiusX + 1) +
                 (y - centerY) * (y - centerY) / (radiusY * radiusY + 1) <= 1) :
                true;

            if (inSelection && IsValidCoord(x, y)) {
                const int sx = x - x1;
                const int sy = y - y1;
                currentSelection.pixels[static_cast<std::size_t>(sy) * currentSelection.width + sx] = GetPixel(x, y);
            }
        }
    }

    currentSelection.isActive = true;
}

void PixelPaintView::ClearSelection()
{
    currentSelection.isActive = false;
    currentSelection.type = SelectionData::Rectangle;
    currentSelection.pixels.clear();
    currentSelection.width = 0;
    currentSelection.height = 0;
    currentSelection.polygonPoints.clear();
}

// Paste selection
void PixelPaintView::PasteSelection(const ImVec2& pastePos)
{
    if (!currentSelection.isActive || currentSelection.pixels.empty()) return;

    int startX = static_cast<int>(pastePos.x);
    int startY = static_cast<int>(pastePos.y);

    size_t pixelIndex = 0;
    int radiusX = currentSelection.width / 2;
    int radiusY = currentSelection.height / 2;
    int centerX = radiusX;
    int centerY = radiusY;

    for (int y = 0; y < currentSelection.height; ++y) {
        for (int x = 0; x < currentSelection.width; ++x) {
            int canvasX = startX + x;
            int canvasY = startY + y;

            if (currentSelection.type == SelectionData::Circle) {
                int dx = x - centerX;
                int dy = y - centerY;
                if ((dx * dx) / (radiusX * radiusX + 1) + (dy * dy) / (radiusY * radiusY + 1) > 1) {
                    pixelIndex++;
                    continue;
                }
            }

            if (IsValidCoord(canvasX, canvasY) && pixelIndex < currentSelection.pixels.size()) {
                PutPixel(canvasX, canvasY, currentSelection.pixels[pixelIndex]);
            }
            pixelIndex++;
        }
    }

    PushUndo("Paste selection");
}

// Blur selection
void PixelPaintView::BlurSelection(float radius)
{
    if (!currentSelection.isActive) return;

    int x1 = static_cast<int>(currentSelection.selectionStart.x);
    int y1 = static_cast<int>(currentSelection.selectionStart.y);
    int x2 = static_cast<int>(currentSelection.selectionEnd.x);
    int y2 = static_cast<int>(currentSelection.selectionEnd.y);

    std::vector<pelpaint::Pixel> blurredData = canvasData;
    int kernelSize = static_cast<int>(radius);

    for (int y = y1; y <= y2; ++y) {
        for (int x = x1; x <= x2; ++x) {
            if (!IsValidCoord(x, y)) continue;

            int r = 0, g = 0, b = 0, a = 0, count = 0;

            for (int ky = -kernelSize; ky <= kernelSize; ++ky) {
                for (int kx = -kernelSize; kx <= kernelSize; ++kx) {
                    int nx = x + kx;
                    int ny = y + ky;

                    if (IsValidCoord(nx, ny)) {
                        pelpaint::Pixel p = canvasData[GetPixelIndex(nx, ny)];
                        r += p.r;
                        g += p.g;
                        b += p.b;
                        a += p.a;
                        count++;
                    }
                }
            }

            if (count > 0) {
                blurredData[GetPixelIndex(x, y)] = pelpaint::Pixel(
                    static_cast<uint8_t>(r / count),
                    static_cast<uint8_t>(g / count),
                    static_cast<uint8_t>(b / count),
                    static_cast<uint8_t>(a / count)
                );
            }
        }
    }

    canvasData = blurredData;
    textureNeedsUpdate = true;
    PushUndo("Blur selection");
}

// Polygon selection - add a point to the polygon
void PixelPaintView::AddPolygonPoint(const ImVec2& point)
{
    currentSelection.polygonPoints.push_back(point);
}

// Clear polygon selection points
void PixelPaintView::ClearPolygonSelection()
{
    currentSelection.polygonPoints.clear();
    currentSelection.isActive = false;
}

// Finalize polygon selection and create selection data
void PixelPaintView::FinalizePolygonSelection()
{
    if (currentSelection.polygonPoints.size() < 3) {
        ClearPolygonSelection();
        return;  // Need at least 3 points for a polygon
    }

    currentSelection.type = SelectionData::Polygon;
    currentSelection.isActive = true;

    // Find bounding box
    float minX = currentSelection.polygonPoints[0].x;
    float maxX = minX;
    float minY = currentSelection.polygonPoints[0].y;
    float maxY = minY;

    for (const auto& point : currentSelection.polygonPoints) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }

    currentSelection.selectionStart = ImVec2(minX, minY);
    currentSelection.selectionEnd = ImVec2(maxX, maxY);
}

// Point-in-polygon test using ray casting algorithm
bool PixelPaintView::IsPointInPolygon(const ImVec2& point, const std::vector<ImVec2>& polygon) const
{
    if (polygon.size() < 3) return false;

    int intersections = 0;
    int n = static_cast<int>(polygon.size());

    for (int i = 0; i < n; ++i) {
        int next = (i + 1) % n;
        const ImVec2& p1 = polygon[i];
        const ImVec2& p2 = polygon[next];

        // Check if ray from point to right crosses edge
        if ((p1.y <= point.y && point.y < p2.y) ||
            (p2.y <= point.y && point.y < p1.y)) {
            // Calculate x intersection
            float xIntersect = p1.x + (point.y - p1.y) * (p2.x - p1.x) / (p2.y - p1.y);
            if (point.x < xIntersect) {
                intersections++;
            }
        }
    }

    // Odd number of intersections = point is inside
    return (intersections % 2) == 1;
}

// Copy polygon selection to clipboard
void PixelPaintView::CopyPolygonSelection()
{
    if (currentSelection.polygonPoints.size() < 3) return;
    if (!currentSelection.isActive) return;

    Layer* activeLayer = GetActiveLayer();
    if (!activeLayer) return;

    int x1 = static_cast<int>(currentSelection.selectionStart.x);
    int y1 = static_cast<int>(currentSelection.selectionStart.y);
    int x2 = static_cast<int>(currentSelection.selectionEnd.x);
    int y2 = static_cast<int>(currentSelection.selectionEnd.y);

    currentSelection.width = x2 - x1 + 1;
    currentSelection.height = y2 - y1 + 1;
    currentSelection.pixels.clear();
    currentSelection.pixels.resize(currentSelection.width * currentSelection.height, pelpaint::Pixel(0, 0, 0, 0));

    // Copy pixels inside polygon
    for (int y = y1; y <= y2; ++y) {
        for (int x = x1; x <= x2; ++x) {
            ImVec2 pixelPos(static_cast<float>(x), static_cast<float>(y));
            if (IsPointInPolygon(pixelPos, currentSelection.polygonPoints)) {
                if (IsValidCoord(x, y)) {
                    int srcIndex = GetPixelIndex(x, y);
                    int dstIndex = (y - y1) * currentSelection.width + (x - x1);
                    if (dstIndex >= 0 && dstIndex < static_cast<int>(currentSelection.pixels.size())) {
                        currentSelection.pixels[dstIndex] = activeLayer->pixelData[srcIndex];
                    }
                }
            }
        }
    }

    currentSelection.canBlur = true;
}

// Keyboard shortcuts
void PixelPaintView::HandleKeyboardShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        Undo();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        Redo();
    }

    // Polygon selection: Press Escape to finalize or clear
    if (currentTool == DrawTool::PolygonSelect) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (!currentSelection.polygonPoints.empty()) {
                FinalizePolygonSelection();
                CopyPolygonSelection();
            }
        }
        // Press Delete to clear current polygon
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            ClearPolygonSelection();
        }
    }
}

// Canvas input handling
void PixelPaintView::HandleCanvasInput()
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        isDrawing = false;
    }

    if (!ImGui::IsItemHovered() || !ImGui::IsWindowHovered()) {
        return;
    }

    ImVec2 canvasMousePos = ScreenToCanvas(mousePos);
    int pixelX = static_cast<int>(canvasMousePos.x);
    int pixelY = static_cast<int>(canvasMousePos.y);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        isDrawing = true;
        lastDrawPoint = canvasMousePos;

        if (currentTool == DrawTool::RectangleSelect || currentTool == DrawTool::CircleSelect) {
            CopySelection(canvasMousePos, canvasMousePos, currentTool == DrawTool::CircleSelect);
        } else if (currentTool == DrawTool::Eyedropper) {
            if (IsValidCoord(pixelX, pixelY)) {
                currentColor = GetPixel(pixelX, pixelY);
            }
        } else if (currentTool == DrawTool::Clone) {
            if (!cloneSourceSet) {
                cloneSourcePoint = ImVec2(static_cast<float>(pixelX), static_cast<float>(pixelY));
                cloneSourceSet = true;
            } else {
                PushUndo("Clone");
            }
        } else if (currentTool == DrawTool::Line) {
            isLineMode = true;
        } else if (currentTool == DrawTool::Fill) {
            isLineMode = true;
        } else if (currentTool == DrawTool::BucketFill) {
            if (IsValidCoord(pixelX, pixelY)) {
                pelpaint::Pixel fillCol = currentColor;
                if (bucketEraseToAlpha) {
                    fillCol = pelpaint::Pixel(0, 0, 0, 0);
                }
                if (bucketThreshold > 0.0f) {
                    FloodFillWithThreshold(pixelX, pixelY, fillCol, bucketThreshold);
                } else {
                    FloodFill(pixelX, pixelY, fillCol);
                }
                PushUndo("Bucket Fill");
            }
        } else if (currentTool == DrawTool::ShapeRedraw) {
            PushUndo("Shape Redraw");
        } else {
            PushUndo("Draw");
        }
    }

    if (isDrawing && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (currentTool == DrawTool::Pencil || currentTool == DrawTool::Eraser) {
            pelpaint::Pixel drawColor = (currentTool == DrawTool::Eraser) ? pelpaint::Pixel(0, 0, 0, 0) : currentColor;

            // Draw line from last point to current point for smooth strokes
            DrawLineBresenham(
                static_cast<int>(lastDrawPoint.x), static_cast<int>(lastDrawPoint.y),
                pixelX, pixelY, drawColor, brushSettings.size
            );

            lastDrawPoint = canvasMousePos;
        } else if (currentTool == DrawTool::Spray) {
            DrawSpray(pixelX, pixelY, brushSettings.size, currentColor, 0.5f);
        } else if (currentTool == DrawTool::Clone && cloneSourceSet) {
            ImVec2 sourceCanvasPos = cloneSourcePoint;
            int srcX = static_cast<int>(sourceCanvasPos.x);
            int srcY = static_cast<int>(sourceCanvasPos.y);

            int radius = static_cast<int>(brushSettings.size / 2.0f);
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dy * dy <= radius * radius) {
                        int sourcePixelX = srcX + dx;
                        int sourcePixelY = srcY + dy;

                        if (IsValidCoord(sourcePixelX, sourcePixelY)) {
                            pelpaint::Pixel sourcePixel = GetPixel(sourcePixelX, sourcePixelY);
                            PutPixel(pixelX + dx, pixelY + dy, sourcePixel);
                        }
                    }
                }
            }
        } else if (currentTool == DrawTool::ShapeRedraw) {
            DrawShapeRedrawShape(pixelX, pixelY, currentColor, shapeRedrawBgColor, shapeRedrawShape, shapeRedrawSize);
        } else if (currentTool == DrawTool::RectangleSelect || currentTool == DrawTool::CircleSelect) {
            CopySelection(ImVec2(static_cast<float>(lastDrawPoint.x), static_cast<float>(lastDrawPoint.y)), canvasMousePos, currentTool == DrawTool::CircleSelect);
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (currentTool == DrawTool::Line && isLineMode) {
            pelpaint::Pixel drawColor = currentColor;
            DrawLineBresenham(
                static_cast<int>(lastDrawPoint.x),
                static_cast<int>(lastDrawPoint.y),
                pixelX,
                pixelY,
                drawColor,
                brushSettings.size
            );
            isLineMode = false;
        } else if (currentTool == DrawTool::Fill && isLineMode) {
            isLineMode = false;
        }
        isDrawing = false;
    }

    // Zoom with mouse wheel (Ctrl + Scroll)
    if (io.MouseWheel != 0.0f && io.KeyCtrl) {
        ImVec2 mousePosOnCanvas = ScreenToCanvas(mousePos);
        float oldScale = canvasScale;

        canvasScale = std::clamp(canvasScale + io.MouseWheel * 0.1f * canvasScale, 0.1f, 20.0f);

        scrollOffset.x += (mousePosOnCanvas.x * (oldScale - canvasScale));
        scrollOffset.y += (mousePosOnCanvas.y * (oldScale - canvasScale));
    }

    // Pan with middle mouse button
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        scrollOffset.x += io.MouseDelta.x;
        scrollOffset.y += io.MouseDelta.y;
    }
}

// Process drawing
void PixelPaintView::ProcessDrawing(const ImVec2& mousePos)
{
    lastDrawPoint = mousePos;
}

// Draw toolbar (left sidebar)
void PixelPaintView::DrawToolbar()
{
    struct ToolButton {
        DrawTool tool;
        const char* label;
        const char* name;
    };

    const ToolButton toolButtons[] = {
        { DrawTool::Pencil, "P", "Pencil" },
        { DrawTool::Eraser, "E", "Eraser" },
        { DrawTool::Line, "L", "Line" },
        { DrawTool::Fill, "F", "Fill" },
        { DrawTool::Eyedropper, "I", "Eyedropper" },
        { DrawTool::Spray, "S", "Spray" },
        { DrawTool::RectangleSelect, "R", "Rect Select" },
        { DrawTool::CircleSelect, "C", "Circle Select" },
        { DrawTool::PolygonSelect, "G", "Polygon Select" },
        { DrawTool::BucketFill, "K", "Bucket Fill" }
    };

    ImGui::BeginGroup();
    {
        float buttonSize = 40.0f;
        int toolCount = static_cast<int>(sizeof(toolButtons) / sizeof(toolButtons[0]));

        for (int i = 0; i < toolCount; ++i) {
            const ToolButton& entry = toolButtons[i];
            DrawTool tool = entry.tool;
            bool selected = (currentTool == tool);

            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
            }

            if (ImGui::Button(entry.label, ImVec2(buttonSize, buttonSize))) {
                if (tool == DrawTool::RectangleSelect || tool == DrawTool::CircleSelect) {
                    if (currentTool == tool) {
                        ClearSelection();
                        currentTool = DrawTool::Pencil;
                    } else {
                        currentTool = tool;
                    }
                } else {
                    if (currentSelection.isActive) {
                        ClearSelection();
                    }
                    currentTool = tool;
                }

                if (tool == DrawTool::Clone) {
                    cloneSourceSet = false;
                }
            }

            if (selected) {
                ImGui::PopStyleColor();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", entry.name);
            }
        }
    }
    ImGui::EndGroup();
}

// Draw color picker panel
void PixelPaintView::DrawColorPicker()
{
    ImGui::Text("Current Color:");
    ImGui::SameLine();

    ImVec4 color = ImVec4(
        currentColor.r / 255.0f,
        currentColor.g / 255.0f,
        currentColor.b / 255.0f,
        currentColor.a / 255.0f
    );

    if (ImGui::ColorEdit4("##color", &color.x)) {
        currentColor.r = static_cast<uint8_t>(color.x * 255);
        currentColor.g = static_cast<uint8_t>(color.y * 255);
        currentColor.b = static_cast<uint8_t>(color.z * 255);
        currentColor.a = static_cast<uint8_t>(color.w * 255);
    }
}

void PixelPaintView::ApplyDithering(DitheringType type, const std::vector<pelpaint::Pixel>& palette)
{
    switch (type) {
        case DitheringType::Atkinson:
            if (atkinsonGrayscaleToMono) ConvertToGrayscale();
            ApplyAtkinsonDithering(palette);
            break;
        case DitheringType::FloydSteinberg:
            if (grayscaleToMono) ConvertToGrayscale();
            ApplyFloydSteinbergDithering(palette);
            break;
        case DitheringType::Ordered:
            if (grayscaleToMono) ConvertToGrayscale();
            ApplyOrderedDithering(palette);
            break;
        case DitheringType::Stucki:
            if (stuckiGrayscaleToMono) ConvertToGrayscale();
            ApplyStuckiDithering(palette);
            break;
        default:
            break;
    }

    textureNeedsUpdate = true;
    PushUndo("Apply dithering");
}


// Draw palette selector
void PixelPaintView::DrawPaletteSelector()
{
    ImGui::Separator();
    ImGui::Text("Color Palette");

    const char* preview = selectedPaletteIndex >= 0 ?
        availablePalettes[selectedPaletteIndex].name.c_str() : "No Palette (Full Color)";

    if (ImGui::BeginCombo("Palette", preview)) {
        if (ImGui::Selectable("No Palette (Full Color)", selectedPaletteIndex == -1)) {
            selectedPaletteIndex = -1;
            paletteEnabled = false;
        }

        for (size_t i = 0; i < availablePalettes.size(); ++i) {
            bool selected = (selectedPaletteIndex == static_cast<int>(i));
            if (ImGui::Selectable(availablePalettes[i].name.c_str(), selected)) {
                selectedPaletteIndex = static_cast<int>(i);
                customPalette = availablePalettes[i].colors;
                paletteEnabled = true;
            }
        }
        selectedPaletteIndex = 0;
        ImGui::EndCombo();
    }

    // Display palette colors for direct picking
    if (!customPalette.empty()) {
        ImGui::Separator();
        ImGui::Text("Palette Colors:");
        int colsPerRow = 8;
        float colorButtonSize = 24.0f;

        for (size_t i = 0; i < customPalette.size(); ++i) {
            const auto& color = customPalette[i];
            ImVec4 buttonColor = ImVec4(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
            ImGui::PushID(static_cast<int>(i) + 10000);  // Offset ID to avoid conflicts
            ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonColor.x * 1.2f, buttonColor.y * 1.2f, buttonColor.z * 1.2f, buttonColor.w));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonColor.x * 0.8f, buttonColor.y * 0.8f, buttonColor.z * 0.8f, buttonColor.w));

            if (ImGui::Button("##palettecolor", ImVec2(colorButtonSize, colorButtonSize))) {
                currentColor = color;
            }

            ImGui::PopStyleColor(3);
            ImGui::PopID();

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("R:%d G:%d B:%d A:%d", color.r, color.g, color.b, color.a);
            }

            if ((i + 1) % colsPerRow != 0 && i + 1 < customPalette.size()) {
                ImGui::SameLine();
            }
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Apply Palette Direct", ImVec2(-1, 0))) {
        if (!customPalette.empty()) {
            ApplyPalette(customPalette);
        }
    }
    // ImGui::SetItemTooltip("Snap every pixel to the nearest palette color (no dithering)");
    // ImGui::TextDisabled("For dithering options, see the Filter tab.");
}

// Draw brush settings
void PixelPaintView::DrawBrushSettings()
{
    ImGui::Separator();
    ImGui::Text("Brush Settings");

    ImGui::Checkbox("##useStepping", &brushSettings.useStepping);
    ImGui::SameLine();
    ImGui::Text("Use Input Mode");

    if (brushSettings.useStepping) {
        static char sizeBuffer[16] = "1";
        if (ImGui::InputText("Size##input", sizeBuffer, sizeof(sizeBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            brushSettings.size = std::max(1.0f, static_cast<float>(std::atof(sizeBuffer)));
        }
    } else {
        pelpaint::ui::SliderFloatStepStateful(
            "Size", 1.0f, 100.0f, 1.0f, "brush_size", brushSettings.size,
            [&](float v){ brushSettings.size = v; }
        );
    }

    pelpaint::ui::SliderFloatStepStateful(
        "Opacity", 0.0f, 1.0f, 0.01f, "brush_opacity", brushSettings.opacity,
        [&](float v){ brushSettings.opacity = v; }
    );
    ImGui::Checkbox("Antialiased", &brushSettings.antialiased);

    if (currentTool == DrawTool::Eraser) {
        ImGui::Separator();
        ImGui::Text("Eraser Mode");
        ImGui::TextDisabled("Brush settings above apply to eraser");
    }

    if (currentTool == DrawTool::BucketFill) {
        ImGui::Separator();
        ImGui::Text("Bucket Fill Settings");
        ImGui::Checkbox("Erase to Alpha##bk_alpha", &bucketEraseToAlpha);
        ImGui::SetItemTooltip("Fill with transparency (alpha 0) to 'erase' a color region");

        if (bucketEraseToAlpha) {
            ImGui::TextDisabled("Current color ignored (filling with 0 alpha)");
        }

        pelpaint::ui::SliderFloatStepStateful(
            "Threshold", 0.0f, 255.0f, 1.0f, "bucket_threshold", bucketThreshold,
            [&](float v){ bucketThreshold = v; }
        );
    }
}

// Draw canvas view
void PixelPaintView::DrawCanvasView()
{
    UpdateTexture();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float fitScaleX = (canvasWidth > 0) ? (avail.x / static_cast<float>(canvasWidth)) : 1.0f;
    const float fitScaleY = (canvasHeight > 0) ? (avail.y / static_cast<float>(canvasHeight)) : 1.0f;
    const float fitScale = std::min(fitScaleX, fitScaleY);
    const float baseScale = fitCanvas ? fitScale : 1.0f;
    const float effectiveScale = std::max(0.01f, baseScale * userCanvasScale);

    ImVec2 imageSize = ImVec2(canvasWidth * effectiveScale, canvasHeight * effectiveScale);

    ImVec2 cursorStart = ImGui::GetCursorScreenPos();
    ImVec2 offset = ImVec2((avail.x - imageSize.x) * 0.5f, (avail.y - imageSize.y) * 0.5f);
    if (fitCanvas && userCanvasScale <= 1.0f) {
        if (offset.x < 0.0f) offset.x = 0.0f;
        if (offset.y < 0.0f) offset.y = 0.0f;
    }

    canvasPos = ImVec2(cursorStart.x + offset.x, cursorStart.y + offset.y);
    ImGui::SetCursorScreenPos(canvasPos);

    const float prevScale = canvasScale;
    canvasScale = effectiveScale;

#if defined(USE_METAL_BACKEND)
    if (metalTexture != nullptr) {
        ImGui::Image(metalTexture, imageSize);
    }
#else
    ImGui::Image(reinterpret_cast<void*>(static_cast<intptr_t>(textureID)), imageSize, ImVec2(0, 1), ImVec2(1, 0));
#endif

    HandleCanvasInput();
    DrawSelectionOverlay();

    if (gridMode != GridMode::None) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 gridColor = ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.3f));
        float fullWidth = canvasWidth * canvasScale;
        float fullHeight = canvasHeight * canvasScale;

        if (gridMode == GridMode::Lines) {
            for (int x = 0; x <= canvasWidth; x += gridSize) {
                float sx = canvasPos.x + x * canvasScale;
                drawList->AddLine(ImVec2(sx, canvasPos.y), ImVec2(sx, canvasPos.y + fullHeight), gridColor);
            }
            for (int y = 0; y <= canvasHeight; y += gridSize) {
                float sy = canvasPos.y + y * canvasScale;
                drawList->AddLine(ImVec2(canvasPos.x, sy), ImVec2(canvasPos.x + fullWidth, sy), gridColor);
            }
        } else if (gridMode == GridMode::Dots || gridMode == GridMode::Crosses) {
            float size = (gridMode == GridMode::Crosses) ? 2.0f : 1.0f;
            for (int y = 0; y <= canvasHeight; y += gridSize) {
                for (int x = 0; x <= canvasWidth; x += gridSize) {
                    float sx = canvasPos.x + x * canvasScale;
                    float sy = canvasPos.y + y * canvasScale;
                    if (gridMode == GridMode::Dots) {
                        drawList->AddCircleFilled(ImVec2(sx, sy), 1.0f, gridColor);
                    } else {
                        drawList->AddLine(ImVec2(sx - size, sy), ImVec2(sx + size, sy), gridColor);
                        drawList->AddLine(ImVec2(sx, sy - size), ImVec2(sx, sy + size), gridColor);
                    }
                }
            }
        }

        // Helper lines: Center
        if (showGridCenter) {
            ImU32 centerColor = ImGui::GetColorU32(ImVec4(0.8f, 0.3f, 0.3f, 0.6f));
            float midX = canvasPos.x + (canvasWidth / 2.0f) * canvasScale;
            float midY = canvasPos.y + (canvasHeight / 2.0f) * canvasScale;
            drawList->AddLine(ImVec2(midX, canvasPos.y), ImVec2(midX, canvasPos.y + fullHeight), centerColor, 2.0f);
            drawList->AddLine(ImVec2(canvasPos.x, midY), ImVec2(canvasPos.x + fullWidth, midY), centerColor, 2.0f);
        }

        // Helper lines: Golden Ratio (Sectio Aurea)
        if (showGridGoldenRatio) {
            ImU32 goldenColor = ImGui::GetColorU32(ImVec4(0.8f, 0.7f, 0.2f, 0.6f));
            float gr = 0.618033f;
            float invGr = 1.0f - gr;
            float xCoords[] = { invGr * fullWidth, gr * fullWidth };
            float yCoords[] = { invGr * fullHeight, gr * fullHeight };

            for (float x : xCoords) {
                drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y), ImVec2(canvasPos.x + x, canvasPos.y + fullHeight), goldenColor, 1.5f);
            }
            for (float y : yCoords) {
                drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y), ImVec2(canvasPos.x + fullWidth, canvasPos.y + y), goldenColor, 1.5f);
            }
        }
    }

    canvasScale = prevScale;
}

// Draw selection overlay
void PixelPaintView::DrawSelectionOverlay()
{
    if (!currentSelection.isActive) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 imageSize = ImVec2(canvasWidth * canvasScale, canvasHeight * canvasScale);

    // Draw dark overlay over entire canvas
    drawList->AddRectFilled(
        canvasPos,
        ImVec2(canvasPos.x + imageSize.x, canvasPos.y + imageSize.y),
        ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.5f))
    );

    // Calculate screen positions for selection bounds
    ImVec2 selStart = CanvasToScreen(currentSelection.selectionStart);
    ImVec2 selEnd = CanvasToScreen(currentSelection.selectionEnd);

    // Draw selection border
    ImU32 borderColor = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 1.0f));

    if (currentSelection.type == SelectionData::Circle) {
        const float cx = (selStart.x + selEnd.x) * 0.5f;
        const float cy = (selStart.y + selEnd.y) * 0.5f;
        const float rx = std::max(1.0f, (selEnd.x - selStart.x) * 0.5f);
        const float ry = std::max(1.0f, (selEnd.y - selStart.y) * 0.5f);

        const int segments = 64;
        std::vector<ImVec2> points;
        points.reserve(segments + 1);
        constexpr float twoPi = 6.2831853f;

        for (int i = 0; i <= segments; ++i) {
            float t = (twoPi * static_cast<float>(i)) / static_cast<float>(segments);
            points.emplace_back(cx + std::cos(t) * rx, cy + std::sin(t) * ry);
        }

        drawList->AddPolyline(points.data(), static_cast<int>(points.size()), borderColor, true, 2.0f);
        return;
    }

    // Draw bright dashed border
    float dashSize = 4.0f;
    float gapSize = 4.0f;
    bool drawDash = true;

    // Top edge
    for (float x = selStart.x; x < selEnd.x; x += dashSize + gapSize) {
        if (drawDash) {
            float endX = std::min(x + dashSize, selEnd.x);
            drawList->AddLine(ImVec2(x, selStart.y), ImVec2(endX, selStart.y), borderColor, 2.0f);
        }
        drawDash = !drawDash;
    }

    // Bottom edge
    drawDash = true;
    for (float x = selStart.x; x < selEnd.x; x += dashSize + gapSize) {
        if (drawDash) {
            float endX = std::min(x + dashSize, selEnd.x);
            drawList->AddLine(ImVec2(x, selEnd.y), ImVec2(endX, selEnd.y), borderColor, 2.0f);
        }
        drawDash = !drawDash;
    }

    // Left edge
    drawDash = true;
    for (float y = selStart.y; y < selEnd.y; y += dashSize + gapSize) {
        if (drawDash) {
            float endY = std::min(y + dashSize, selEnd.y);
            drawList->AddLine(ImVec2(selStart.x, y), ImVec2(selStart.x, endY), borderColor, 2.0f);
        }
        drawDash = !drawDash;
    }

    // Right edge
    drawDash = true;
    for (float y = selStart.y; y < selEnd.y; y += dashSize + gapSize) {
        if (drawDash) {
            float endY = std::min(y + dashSize, selEnd.y);
            drawList->AddLine(ImVec2(selEnd.x, y), ImVec2(selEnd.x, endY), borderColor, 2.0f);
        }
        drawDash = !drawDash;
    }
}

// Draw status bar
void PixelPaintView::DrawStatusBar()
{
    const char* toolName = "Unknown";
    switch (currentTool) {
        case DrawTool::Pencil: toolName = "Pencil"; break;
        case DrawTool::Eraser: toolName = "Eraser"; break;
        case DrawTool::Line: toolName = "Line"; break;
        case DrawTool::Fill: toolName = "Fill"; break;
        case DrawTool::Eyedropper: toolName = "Eyedropper"; break;
        case DrawTool::Spray: toolName = "Spray"; break;
        case DrawTool::RectangleSelect: toolName = "Rect Select"; break;
        case DrawTool::CircleSelect: toolName = "Circle Select"; break;
        case DrawTool::BucketFill: toolName = "Bucket Fill"; break;
        case DrawTool::Clone: {
            toolName = cloneSourceSet ? "Clone (Ready)" : "Clone (Set Source)";
            break;
        }
        case DrawTool::ShapeRedraw: toolName = "Shape Redraw"; break;
        case DrawTool::PolygonSelect: toolName = "Polygon Select"; break;
    }

    // Status text on the left
    std::string toolDisplay = toolName;
    if (currentTool == DrawTool::Eraser && eraserUseAlpha) toolDisplay += " (Alpha)";
    if (currentTool == DrawTool::BucketFill && bucketEraseToAlpha) toolDisplay += " (Alpha)";

    ImGui::Text("Tool: %s | Brush: %.1f | Zoom: %.1fx | Color: #%02X%02X%02X%02X",
        toolDisplay.c_str(),
        brushSettings.size,
        canvasScale,
        currentColor.r,
        currentColor.g,
        currentColor.b,
        currentColor.a
    );

    // Collapse/Expand button on the right
    ImGui::SameLine();
    float availWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availWidth - 30.0f);
    if (ImGui::Button(rightPanelCollapsed ? "Show" : "Hide", ImVec2(40, 0))) {
        rightPanelCollapsed = !rightPanelCollapsed;
    }
}

// RIGHT PANEL WITH TABS
void PixelPaintView::DrawRightPanel()
{
    ImGui::BeginChild("RightPanel", ImVec2(rightPanelWidth, 0), true, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));

        if (ImGui::BeginTabBar("RightPanelTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Tool")) {
                DrawToolTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Color")) {
                DrawColorTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Filter")) {
                DrawFilterTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Layers")) {
                DrawLayersTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Files")) {
                DrawFilesTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

// TOOL TAB - Brush settings and editing tools
void PixelPaintView::DrawToolTab()
{
    // Edit section
    if (ImGui::CollapsingHeader("Edit", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Undo (Ctrl+Z)", ImVec2(-1, 0))) { Undo(); }
        if (ImGui::Button("Redo (Ctrl+Y)", ImVec2(-1, 0))) { Redo(); }
        ImGui::Separator();

        if (IsRectSelectionActive()) {
            if (ImGui::Button("Crop to Selection", ImVec2(-1, 0))) {
                CropToSelection();
            }
            ImGui::Separator();
        }

        if (ImGui::Button("Clear Canvas", ImVec2(-1, 0))) { ClearCanvas(); }
    }
    ImGui::Spacing();

    // Brush Settings section
    if (ImGui::CollapsingHeader("Brush Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawBrushSettings();
    }
    ImGui::Spacing();

    // View section
    if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Fit Canvas##fit", &fitCanvas);
        pelpaint::ui::SliderFloatStepStateful(
            "Zoom", 0.1f, 20.0f, 0.1f, "view_zoom", userCanvasScale,
            [&](float v){ userCanvasScale = v; }
        );
        ImGui::Separator();

        ImGui::Text("Grid Mode:");
        int mode = (int)gridMode;
        if (ImGui::RadioButton("None", &mode, 0)) gridMode = GridMode::None; ImGui::SameLine();
        if (ImGui::RadioButton("Lines", &mode, 1)) gridMode = GridMode::Lines; ImGui::SameLine();
        if (ImGui::RadioButton("Dots", &mode, 2)) gridMode = GridMode::Dots; ImGui::SameLine();
        if (ImGui::RadioButton("Cross", &mode, 3)) gridMode = GridMode::Crosses;

        if (gridMode != GridMode::None) {
            pelpaint::ui::SliderIntStepStateful(
                "Grid Size", 2, 64, 1, "grid_size", gridSize,
                [&](int v){ gridSize = v; }
            );
            ImGui::Checkbox("Show Center", &showGridCenter);
            ImGui::SameLine();
            ImGui::Checkbox("Golden Ratio", &showGridGoldenRatio);
        }
    }
    ImGui::Spacing();

    // Shape Redraw brush tool section
    if (ImGui::CollapsingHeader("Shape Redraw", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool isActive = (currentTool == DrawTool::ShapeRedraw);
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
        }
        if (ImGui::Button(isActive ? "Active (Shape Redraw)" : "Activate Shape Redraw", ImVec2(-1, 0))) {
            currentTool = DrawTool::ShapeRedraw;
        }
        if (isActive) ImGui::PopStyleColor();

        ImGui::Spacing();

        // Background color selector
        ImGui::Text("Background Color:");
        ImVec4 bgColorVec = ImVec4(
            shapeRedrawBgColor.r / 255.0f,
            shapeRedrawBgColor.g / 255.0f,
            shapeRedrawBgColor.b / 255.0f,
            shapeRedrawBgColor.a / 255.0f
        );
        if (ImGui::ColorButton("##shapeRedrawBg", bgColorVec, ImGuiColorEditFlags_NoAlpha, ImVec2(36, 36))) {
            ImGui::OpenPopup("ShapeRedrawBgColorPicker");
        }
        ImGui::SameLine();
        ImGui::Text("RGB(%d,%d,%d)", shapeRedrawBgColor.r, shapeRedrawBgColor.g, shapeRedrawBgColor.b);

        if (ImGui::BeginPopup("ShapeRedrawBgColorPicker")) {
            float col[3] = {
                shapeRedrawBgColor.r / 255.0f,
                shapeRedrawBgColor.g / 255.0f,
                shapeRedrawBgColor.b / 255.0f
            };
            if (ImGui::ColorPicker3("##srbgpicker", col)) {
                shapeRedrawBgColor.r = static_cast<uint8_t>(col[0] * 255);
                shapeRedrawBgColor.g = static_cast<uint8_t>(col[1] * 255);
                shapeRedrawBgColor.b = static_cast<uint8_t>(col[2] * 255);
            }
            ImGui::Separator();
            ImGui::Text("Quick:");
            if (ImGui::Button("Black##srbg",  ImVec2(-1, 0))) { shapeRedrawBgColor = pelpaint::Pixel(0,   0,   0,   255); ImGui::CloseCurrentPopup(); }
            if (ImGui::Button("White##srbg",  ImVec2(-1, 0))) { shapeRedrawBgColor = pelpaint::Pixel(255, 255, 255, 255); ImGui::CloseCurrentPopup(); }
            if (ImGui::Button("Pink##srbg",   ImVec2(-1, 0))) { shapeRedrawBgColor = pelpaint::Pixel(255, 0,   255, 255); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        ImGui::Spacing();

        // Shape selector
        ImGui::Text("Shape:");
        static int selectedShape = 0;
        ImGui::RadioButton("Dot##srshape",         &selectedShape, 0);
        ImGui::RadioButton("Circle##srshape",      &selectedShape, 1);
        ImGui::RadioButton("Rectangle##srshape",   &selectedShape, 2);
        ImGui::RadioButton("Rect Outline##srshape",&selectedShape, 3);
        ImGui::RadioButton("Gem/Diamond##srshape", &selectedShape, 4);

        switch (selectedShape) {
            case 0: shapeRedrawShape = ShapeRedrawShape::Dot;        break;
            case 1: shapeRedrawShape = ShapeRedrawShape::Circle;     break;
            case 2: shapeRedrawShape = ShapeRedrawShape::Rectangle;  break;
            case 3: shapeRedrawShape = ShapeRedrawShape::RectOutline;break;
            case 4: shapeRedrawShape = ShapeRedrawShape::Gem;        break;
        }

        ImGui::Spacing();
        pelpaint::ui::SliderIntStepStateful(
            "Shape Size", 1, 32, 1, "shape_redraw_size", shapeRedrawSize,
            [&](int v){ shapeRedrawSize = v; }
        );
        ImGui::Checkbox("Auto-Shade##sr", &shapeRedrawAutoShade);
        ImGui::SetItemTooltip("Automatically shade edges based on background brightness");

        ImGui::Spacing();
        ImGui::TextDisabled("Drag on canvas to paint shapes.");
    }
}

// COLOR TAB - Color picker and palette
void PixelPaintView::DrawColorTab()
{
    if (ImGui::CollapsingHeader("Current Color", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawColorPicker();
    }
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Color Palette", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawPaletteSelector();
    }
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Recent Colors", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!frequentColors.empty()) {
            int colsPerRow = 4;
            float colorButtonSize = 28.0f;
            for (size_t i = 0; i < frequentColors.size(); ++i) {
                const auto& color = frequentColors[i];
                ImVec4 buttonColor = ImVec4(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
                ImGui::PushID(static_cast<int>(i));
                ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonColor.x * 1.2f, buttonColor.y * 1.2f, buttonColor.z * 1.2f, buttonColor.w));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonColor.x * 0.8f, buttonColor.y * 0.8f, buttonColor.z * 0.8f, buttonColor.w));
                if (ImGui::Button("##color", ImVec2(colorButtonSize, colorButtonSize))) {
                    currentColor = color;
                }
                ImGui::PopStyleColor(3);
                ImGui::PopID();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("R:%d G:%d B:%d A:%d", color.r, color.g, color.b, color.a);
                }
                if ((i + 1) % colsPerRow != 0 && i + 1 < frequentColors.size()) {
                    ImGui::SameLine();
                }
            }
        } else {
            ImGui::TextDisabled("(no colors yet)");
        }
    }
}

// FILTER TAB - Convert, Dithering, Pixelify, Shape Redraw filter
void PixelPaintView::DrawFilterTab()
{
    // ----------------------------------------------------------------
    // Convert
    // ----------------------------------------------------------------
    if (ImGui::CollapsingHeader("Convert", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("To Grayscale", ImVec2(-1, 0))) {
            ConvertToGrayscale();
        }
    }
    ImGui::Spacing();

    // ----------------------------------------------------------------
    // Dithering
    // ----------------------------------------------------------------
    if (ImGui::CollapsingHeader("Dithering", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Preserve Alpha##dither", &ditheringPreserveAlpha);
        ImGui::Spacing();

        ImGui::Text("Method:");
        ImGui::RadioButton("Floyd-Steinberg##dm", &selectedDitheringMethod, 0);
        ImGui::RadioButton("Atkinson##dm",        &selectedDitheringMethod, 1);
        ImGui::RadioButton("Stucki##dm",          &selectedDitheringMethod, 2);
        ImGui::RadioButton("Ordered##dm",         &selectedDitheringMethod, 3);

        ImGui::Spacing();

        // Per-method options
        if (selectedDitheringMethod == 0 || selectedDitheringMethod == 3) {
            // Floyd-Steinberg / Ordered share the same grayscale-to-mono flag
            ImGui::Checkbox("Grayscale to Mono##dm_fs", &grayscaleToMono);
            ImGui::SetItemTooltip("Convert to grayscale before dithering");
        }
        if (selectedDitheringMethod == 1) {
            ImGui::Checkbox("Grayscale to Mono##dm_at", &atkinsonGrayscaleToMono);
            ImGui::SetItemTooltip("Convert to grayscale before Atkinson dithering");
            pelpaint::ui::SliderIntStepStateful(
                "Matrix Distance", 1, 10, 1, "atkinson_matrix_distance", atkinsonMatrixDistance,
                [&](int v){ atkinsonMatrixDistance = v; }
            );
            ImGui::SetItemTooltip("Error diffusion spread distance");
        }
        if (selectedDitheringMethod == 2) {
            ImGui::Checkbox("Grayscale to Mono##dm_st", &stuckiGrayscaleToMono);
            ImGui::SetItemTooltip("Convert to grayscale before Stucki dithering");
            pelpaint::ui::SliderIntStepStateful(
                "Matrix Distance", 1, 10, 1, "stucki_matrix_distance", stuckiMatrixDistance,
                [&](int v){ stuckiMatrixDistance = v; }
            );
            ImGui::SetItemTooltip("Error diffusion spread distance");
        }

        ImGui::Spacing();
        if (ImGui::Button("Apply Dithering##apply", ImVec2(-1, 0))) {
            if (selectedPaletteIndex < 0 || (!paletteEnabled && customPalette.empty())) {
                ImGui::OpenPopup("NoPaletteWarning");
            } else {
                const auto& pal = customPalette.empty()
                    ? availablePalettes[selectedPaletteIndex].colors
                    : customPalette;
                DitheringType method;
                switch (selectedDitheringMethod) {
                    case 1:  method = DitheringType::Atkinson;       break;
                    case 2:  method = DitheringType::Stucki;         break;
                    case 3:  method = DitheringType::Ordered;        break;
                    default: method = DitheringType::FloydSteinberg; break;
                }
                ApplyDithering(method, pal);
            }
        }

        if (ImGui::BeginPopupModal("NoPaletteWarning", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Please select a palette before applying dithering.");
            if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    ImGui::Spacing();

    // ----------------------------------------------------------------
    // Pixelify
    // ----------------------------------------------------------------
    if (ImGui::CollapsingHeader("Pixelify", ImGuiTreeNodeFlags_DefaultOpen)) {
        pelpaint::ui::SliderIntStepStateful(
            "Size", 1, 32, 1, "pixelify_size", pixelifySize,
            [&](int v){ pixelifySize = v; }
        );
        ImGui::Checkbox("Apply Palette##pixelify", &pixelifyUsePalette);
        ImGui::SetItemTooltip("Quantize averaged block color to the current palette");
        ImGui::Spacing();
        if (ImGui::Button("Apply Pixelify##button", ImVec2(-1, 0))) {
            ApplyPixelify(pixelifySize, pixelifyUsePalette);
        }
        ImGui::TextDisabled("Averages color blocks; optionally snaps to palette.");
    }
    ImGui::Spacing();

    // ----------------------------------------------------------------
    // Shape Redraw Filter
    // ----------------------------------------------------------------
    if (ImGui::CollapsingHeader("Shape Redraw Filter", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Shape mode combo
        const char* shapeModeNames[] = { "Square", "Dot (Circle)", "Custom (8x8)" };
        int shapeMode = static_cast<int>(shapeRedrawFilterMode);
        if (ImGui::Combo("Shape##srf", &shapeMode, shapeModeNames, 3)) {
            shapeRedrawFilterMode = static_cast<ShapeRedrawFilterMode>(shapeMode);
        }

        ImGui::Spacing();

        // Background mode radio buttons
        ImGui::Text("Background:");
        int bgMode = static_cast<int>(shapeRedrawBgMode);
        ImGui::RadioButton("Black##srfbg",  &bgMode, 0); ImGui::SameLine();
        ImGui::RadioButton("White##srfbg",  &bgMode, 1); ImGui::SameLine();
        ImGui::RadioButton("Alpha##srfbg",  &bgMode, 2);
        shapeRedrawBgMode = static_cast<ShapeRedrawBgMode>(bgMode);

        ImGui::Spacing();

        pelpaint::ui::SliderIntStepStateful(
            "Block Size", 2, 64, 1, "shape_redraw_filter_block_size", shapeRedrawFilterBlockSize,
            [&](int v){ shapeRedrawFilterBlockSize = v; }
        );
        ImGui::SetItemTooltip("Size of each sampled block in pixels");
        pelpaint::ui::SliderIntStepStateful(
            "Padding", 0, 16, 1, "shape_redraw_filter_padding", shapeRedrawFilterPadding,
            [&](int v){ shapeRedrawFilterPadding = v; }
        );
        ImGui::SetItemTooltip("Gap between shapes");

        ImGui::Checkbox("Use Palette##srf", &shapeRedrawFilterUsePalette);
        ImGui::SetItemTooltip("Snap block color to the current palette");

        // Custom 8x8 shape editor – shown only when Custom is selected
        if (shapeRedrawFilterMode == ShapeRedrawFilterMode::Custom) {
            ImGui::Spacing();
            ImGui::Text("Custom Shape (8x8):");
            ImGui::SetItemTooltip("Click cells to toggle foreground / background");

            const float cellSize = 22.0f;
            const float cellPad  = 2.0f;
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(cellPad, cellPad));
            for (int row = 0; row < 8; ++row) {
                for (int col = 0; col < 8; ++col) {
                    bool on = shapeRedrawCustomMap[row * 8 + col];
                    ImGui::PushID(row * 8 + col);
                    if (on) {
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.9f, 0.7f, 0.1f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.85f,0.3f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.7f, 0.5f, 0.0f, 1.0f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f,0.15f,0.15f,1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    }
                    if (ImGui::Button("##cell", ImVec2(cellSize, cellSize))) {
                        shapeRedrawCustomMap[row * 8 + col] = !on;
                    }
                    ImGui::PopStyleColor(3);
                    ImGui::PopID();
                    if (col < 7) ImGui::SameLine();
                }
            }
            ImGui::PopStyleVar();

            ImGui::Spacing();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Button("Clear##srfcustom",  ImVec2(80, 0))) {
                shapeRedrawCustomMap.fill(false);
            }
            ImGui::SameLine();
            if (ImGui::Button("Fill##srfcustom",   ImVec2(80, 0))) {
                shapeRedrawCustomMap.fill(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Dot##srfcustom",    ImVec2(80, 0))) {
                // Reset to default dot pattern
                shapeRedrawCustomMap.fill(false);
                for (int r = 0; r < 8; ++r)
                    for (int c = 0; c < 8; ++c) {
                        int dr = r - 3, dc = c - 3;
                        if (dr * dr + dc * dc <= 9)
                            shapeRedrawCustomMap[r * 8 + c] = true;
                    }
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Apply Shape Redraw##srfapply", ImVec2(-1, 0))) {
            ApplyShapeRedrawFilter();
        }
        ImGui::TextDisabled("Redraws the image using block-sampled shapes.");
    }
}

// FILES TAB - File I/O operations
void PixelPaintView::DrawFilesTab()
{
    ImGui::Text("Filename: %s", currentFilename.c_str());
    ImGui::Spacing();

#if TARGET_OS_IOS || TARGET_OS_TV
    auto buildCompositeView = [&](ImageView& view, std::vector<Pixel>& composite) -> bool {
        CompositeLayers(composite);

        view.data = reinterpret_cast<const std::uint8_t*>(composite.data());
        view.width = static_cast<std::uint32_t>(canvasWidth);
        view.height = static_cast<std::uint32_t>(canvasHeight);
        view.stride = view.width * 4;
        view.channels = 4;

        return view.valid();
    };

    auto getDocumentsPath = [&]() -> std::string {
        char* path = iOS_GetDocumentsPath();
        if (!path) return {};
        std::string result(path);
        std::free(path);
        return result;
    };

    auto writePngToMemory = [&](std::vector<std::uint8_t>& out) -> bool {
        ImageView view;
        std::vector<Pixel> composite;
        if (!buildCompositeView(view, composite)) return false;

        out.clear();
        auto writer = [](void* context, void* data, int size) {
            auto* buffer = static_cast<std::vector<std::uint8_t>*>(context);
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            buffer->insert(buffer->end(), bytes, bytes + size);
        };

        return stbi_write_png_to_func(writer, &out,
                                      static_cast<int>(view.width),
                                      static_cast<int>(view.height),
                                      4,
                                      view.data,
                                      static_cast<int>(view.stride)) != 0;
    };

    auto readFileBytes = [&](const std::string& path, std::vector<std::uint8_t>& out) -> bool {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        file.seekg(0, std::ios::end);
        const std::streamsize size = file.tellg();
        if (size <= 0) return false;
        file.seekg(0, std::ios::beg);
        out.resize(static_cast<std::size_t>(size));
        return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
    };



    auto saveTgaToDocuments = [&](const std::string& filename) -> bool {
        std::string docs = getDocumentsPath();
        if (docs.empty()) return false;

        ImageView view;
        std::vector<Pixel> composite;
        if (!buildCompositeView(view, composite)) return false;

        const std::string path = docs + "/" + filename;
        return ImageExporter::SaveToTGA(path, view);
    };

    if (ImGui::Button("Share PNG", ImVec2(-1, 0))) {
        std::vector<std::uint8_t> bytes;
        if (writePngToMemory(bytes)) {
            iOS_SaveFile((currentFilename + ".png").c_str(), bytes.data(), bytes.size());
        }
    }
    if (ImGui::Button("Share TGA", ImVec2(-1, 0))) {
        std::string docs = getDocumentsPath();
        if (!docs.empty()) {
            const std::string filename = currentFilename + ".tga";
            if (saveTgaToDocuments(filename)) {
                std::vector<std::uint8_t> bytes;
                const std::string path = docs + "/" + filename;
                if (readFileBytes(path, bytes)) {
                    iOS_SaveFile(filename.c_str(), bytes.data(), bytes.size());
                }
            }
        }
    }
    if (ImGui::Button("Share Binary", ImVec2(-1, 0))) {
        SaveBinary(currentFilename + ".pxl");
    }
    if (ImGui::Button("Save to Files", ImVec2(-1, 0))) {
        std::vector<std::uint8_t> bytes;
        if (writePngToMemory(bytes)) {
            iOS_SaveFile((currentFilename + ".png").c_str(), bytes.data(), bytes.size());
        }
    }
    if (ImGui::Button("Open File", ImVec2(-1, 0))) {
        iOS_OpenFilePickerWithContext(this, &PixelPaintView::IOSOpenFileCallback);
    }
#elif defined(__EMSCRIPTEN__)
    // Web/Emscripten: Use FileChooser abstraction for file dialogs
    ImGui::Text("Export");
    const char* exportTypes[] = { "Image", "SVG", "Depth Map", "Mesh" };
    ImGui::Combo("Export Type", &exportTypeIndex, exportTypes, static_cast<int>(sizeof(exportTypes) / sizeof(exportTypes[0])));

    if (exportTypeIndex == 0) {
        const char* imageFormats[] = { "PNG", "TGA" };
        if (imageExportFormat < 0 || imageExportFormat >= 2) imageExportFormat = 0;
        ImGui::Combo("Image Format", &imageExportFormat, imageFormats, 2);

        if (ImGui::Button("Save As", ImVec2(-1, 0))) {
            if (imageExportFormat == 0) {
                FileChooser::Instance().SaveFileDialog(
                    "Save PNG", ".png", currentFilename + ".png", "",
                    [this](const std::string& filepath) {
                        if (!filepath.empty()) SaveToPNG(filepath);
                    }
                );
            } else {
                FileChooser::Instance().SaveFileDialog(
                    "Save TGA", ".tga", currentFilename + ".tga", "",
                    [this](const std::string& filepath) {
                        if (!filepath.empty()) SaveToTGA(filepath);
                    }
                );
            }
        }
    } else if (exportTypeIndex == 1) {
        if (ImGui::Button("Save SVG Pixel", ImVec2(-1, 0))) {
            FileChooser::Instance().SaveFileDialog(
                "Save SVG Pixel", ".svg", currentFilename + ".svg", "",
                [this](const std::string& filepath) {
                    if (!filepath.empty()) SaveToSVGPixel(filepath);
                }
            );
        }
        if (ImGui::Button("Save SVG Vector", ImVec2(-1, 0))) {
            FileChooser::Instance().SaveFileDialog(
                "Save SVG Vector", ".svg", currentFilename + ".svg", "",
                [this](const std::string& filepath) {
                    if (!filepath.empty()) SaveToSVGVector(filepath);
                }
            );
        }
    } else if (exportTypeIndex == 2) {
        ImGui::Text("Depth Map");
        pelpaint::ui::SliderIntStepStateful(
            "Depth Map Grid Size", 1, 128, 1, "depth_map_grid_size", depthMapGridSize,
            [&](int v){ depthMapGridSize = v; }
        );
        depthMapGridSize = std::max(1, depthMapGridSize);
        if (ImGui::Button("Export Depth Map", ImVec2(-1, 0))) {
            FileChooser::Instance().SaveFileDialog(
                "Save Depth Map", ".png", currentFilename + ".png", "",
                [this](const std::string& filepath) {
                    if (!filepath.empty()) SaveDepthMap(filepath);
                }
            );
        }
    } else if (exportTypeIndex == 3) {
        ImGui::Text("Mesh");
        const char* meshModes[] = { "Solid", "Wireframe", "LoPoly", "PixelPerfect" };
        ImGui::Combo("Mesh Type", &meshExportMode, meshModes, static_cast<int>(sizeof(meshModes) / sizeof(meshModes[0])));

        const char* meshFormats[] = { "PLY" };
        ImGui::Combo("Mesh Format", &meshExportFormat, meshFormats, 1);

        pelpaint::ui::SliderIntStepStateful(
            "Mesh Grid Size", 1, 128, 1, "mesh_export_grid_size", meshExportGridSize,
            [&](int v){ meshExportGridSize = v; }
        );
        meshExportGridSize = std::max(1, meshExportGridSize);
        if (ImGui::Button("Export Mesh", ImVec2(-1, 0))) {
            FileChooser::Instance().SaveFileDialog(
                "Save Mesh", ".ply", currentFilename + ".ply", "",
                [this](const std::string& filepath) {
                    if (!filepath.empty()) SaveMesh(filepath);
                }
            );
        }
    }

    if (ImGui::Button("Load Image", ImVec2(-1, 0))) {
        FileChooser::Instance().OpenFileDialog(
            "Load Image", ".tga,.png,.jpg,.jpeg", "",
            [this](const std::string& filepath) {
                if (!filepath.empty()) LoadFromImage(filepath);
            }
        );
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
#else
    // Use lastDirectory instead of "." to remember previous location
    std::string startDir = lastDirectory.empty() ? GetHomeDirectory() : lastDirectory;

    ImGui::Text("Export");
    const char* exportTypes[] = { "Image", "SVG", "Depth Map", "Mesh" };
    ImGui::Combo("Export Type", &exportTypeIndex, exportTypes, static_cast<int>(sizeof(exportTypes) / sizeof(exportTypes[0])));

    if (exportTypeIndex == 0) {
        const char* imageFormats[] = { "PNG", "TGA" };
        if (imageExportFormat < 0 || imageExportFormat >= 2) imageExportFormat = 0;
        ImGui::Combo("Image Format", &imageExportFormat, imageFormats, 2);

        if (ImGui::Button("Save As", ImVec2(-1, 0))) {
            if (imageExportFormat == 0) {
                ImGuiFileDialog::Instance()->OpenDialog("SavePNGDialog", "Save PNG", ".png", startDir, 1, nullptr, ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite);
            } else {
                ImGuiFileDialog::Instance()->OpenDialog("SaveTGADialog", "Save TGA", ".tga", startDir, 1, nullptr, ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite);
            }
        }
    } else if (exportTypeIndex == 1) {
        if (ImGui::Button("Save SVG Pixel", ImVec2(-1, 0))) {
            ImGuiFileDialog::Instance()->OpenDialog("SaveSVGPixelDialog", "Save SVG Pixel", ".svg", startDir, 1, nullptr, ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite);
        }
        if (ImGui::Button("Save SVG Vector", ImVec2(-1, 0))) {
            ImGuiFileDialog::Instance()->OpenDialog("SaveSVGVectorDialog", "Save SVG Vector", ".svg", startDir, 1, nullptr, ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite);
        }
    } else if (exportTypeIndex == 2) {
        ImGui::Text("Depth Map");
        pelpaint::ui::SliderIntStepStateful(
            "Depth Map Grid Size", 1, 128, 1, "depth_map_grid_size", depthMapGridSize,
            [&](int v){ depthMapGridSize = v; }
        );
        depthMapGridSize = std::max(1, depthMapGridSize);
        if (ImGui::Button("Export Depth Map", ImVec2(-1, 0))) {
            ImGuiFileDialog::Instance()->OpenDialog("SaveDepthMapDialog", "Save Depth Map", ".png", startDir, 1, nullptr, ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite);
        }
    } else if (exportTypeIndex == 3) {
        ImGui::Text("Mesh");
        const char* meshModes[] = { "Solid", "Wireframe", "LoPoly", "PixelPerfect" };
        ImGui::Combo("Mesh Type", &meshExportMode, meshModes, static_cast<int>(sizeof(meshModes) / sizeof(meshModes[0])));

        const char* meshFormats[] = { "PLY" };
        ImGui::Combo("Mesh Format", &meshExportFormat, meshFormats, 1);

        pelpaint::ui::SliderIntStepStateful(
            "Mesh Grid Size", 1, 128, 1, "mesh_export_grid_size", meshExportGridSize,
            [&](int v){ meshExportGridSize = v; }
        );
        meshExportGridSize = std::max(1, meshExportGridSize);
        if (ImGui::Button("Export Mesh", ImVec2(-1, 0))) {
            ImGuiFileDialog::Instance()->OpenDialog("SaveMeshDialog", "Save Mesh", ".ply", startDir, 1, nullptr, ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite);
        }
    }

    if (ImGui::Button("Load Image", ImVec2(-1, 0))) {
        ImGuiFileDialog::Instance()->OpenDialog("LoadImageDialog", "Load Image", ".tga,.png,.jpg,.jpeg", startDir, 1, nullptr, ImGuiFileDialogFlags_Modal);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
#endif

    // Auto-Pixelify on Load settings
    if (ImGui::CollapsingHeader("Auto-Pixelify on Load", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable Auto-Pixelify##onload", &autoPixelifyOnLoad);
        ImGui::SetItemTooltip("Automatically pixelify large images when loading to prevent memory issues");

        pelpaint::ui::SliderIntStepStateful(
            "Size Threshold", 256, 2048, 1, "auto_pixelify_threshold", autoPixelifyThreshold,
            [&](int v){ autoPixelifyThreshold = v; }
        );
        ImGui::SetItemTooltip("Images wider than this threshold will trigger auto-pixelify\nSuggested: 800-1200 pixels");

        ImGui::Spacing();

        ImGui::TextWrapped("Current Pixelify Size: %d", pixelifySize);
        ImGui::TextWrapped("If enabled, images larger than %d pixels will be automatically pixelified with calculated block size to prevent crashes.", autoPixelifyThreshold);
        ImGui::TextWrapped("pelpaint::Pixel size is calculated based on image dimensions. A palette is automatically selected if none is chosen.");
    }
}



void PixelPaintView::DrawLayersTab()
{
    pelpaint::ui::LayerPanel(
        layers,
        activeLayerIndex,
        nextLayerId,
        [&](const std::string& name) { AddLayer(name); },
        [&](int index) { RemoveLayer(index); },
        [&](int from, int to) { ReorderLayers(from, to); },
        [&]() { RenderLayerToCanvas(); }
    );
}





void PixelPaintView::Draw(std::string_view label)
{
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

    ImVec2 screenSize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(screenSize);
    ImGui::SetNextWindowPos(ImVec2(0, 0));

    ImGui::Begin(label.data(), nullptr, windowFlags);

    HandleKeyboardShortcuts();
    UpdateFrequentColors();

    const float statusBarHeight = 30.0f;
    const bool isLandscape = screenSize.x > screenSize.y;
    const bool overlayToolbar = isLandscape;
    const bool overlayPanels = fitCanvas;
    const float leftToolbarWidth = overlayToolbar ? 0.0f : 50.0f;
    const float toolbarPaddingX = isLandscape ? 12.0f : 0.0f;
    float topPadding = 8.0f;
    float bottomPadding = 8.0f;
#if TARGET_OS_IOS || TARGET_OS_TV
    topPadding = 24.0f;
    bottomPadding = 20.0f;
#endif
    const float contentHeight = screenSize.y - statusBarHeight - topPadding - bottomPadding;

    ImGui::SetCursorPos(ImVec2(0.0f, topPadding));

    // LEFT TOOLBAR (thin, vertical) - tools only
    if (!overlayToolbar) {
        ImGui::BeginChild("LeftToolbar", ImVec2(leftToolbarWidth, contentHeight), true, ImGuiWindowFlags_NoScrollbar);
        {
            DrawToolbar();
        }
        ImGui::EndChild();

        ImGui::SameLine();
    }

    // CENTER CANVAS AREA - main drawing space
    const float panelWidth = (!rightPanelCollapsed && !overlayPanels) ? (rightPanelWidth + 6.0f) : 0.0f;
    float canvasAreaWidth = screenSize.x - leftToolbarWidth - panelWidth - 6.0f;
    ImGui::BeginChild("CanvasArea", ImVec2(canvasAreaWidth, contentHeight), true, ImGuiWindowFlags_NoScrollbar);
    {
        DrawCanvasView();

        // Canvas context menu
        if (ImGui::BeginPopupContextWindow("CanvasContextMenu")) {
            if (IsRectSelectionActive()) {
                if (ImGui::MenuItem("Crop to Selection")) {
                    CropToSelection();
                }
                ImGui::Separator();
            }

            if (ImGui::MenuItem("Undo", "Ctrl+Z")) { Undo(); }
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) { Redo(); }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    // Floating toolbar overlay in landscape (left padded, vertically centered)
    if (overlayToolbar) {
        const float buttonSize = 30.0f;
        const int toolCount = 9;
        const float spacingY = ImGui::GetStyle().ItemSpacing.y;
        const float toolbarHeight = (toolCount * buttonSize) + ((toolCount - 1) * spacingY);
        const float toolbarY = topPadding + std::max(0.0f, (contentHeight - toolbarHeight) * 0.5f);

        ImGui::SetCursorScreenPos(ImVec2(toolbarPaddingX, toolbarY));
        ImGui::BeginChild("FloatingToolbar", ImVec2(40.0f, toolbarHeight + 2.0f), true, ImGuiWindowFlags_NoScrollbar);
        {
            DrawToolbar();
        }
        ImGui::EndChild();
    }

    // RIGHT PANEL - overlay in Fit mode, docked otherwise (collapsible)
    if (!rightPanelCollapsed) {
        ImVec2 panelPos;
        if (overlayPanels) {
            panelPos = ImVec2(screenSize.x - rightPanelWidth - 6.0f, topPadding);
            ImGui::SetCursorScreenPos(panelPos);
        } else {
            ImGui::SameLine();
            ImGui::SetCursorPosY(topPadding);
            panelPos = ImGui::GetCursorScreenPos();
        }

        DrawRightPanel();

        if (overlayPanels) {
            ImVec2 dividerStart = ImVec2(panelPos.x - 5.0f, panelPos.y);
            ImVec2 dividerEnd = ImVec2(panelPos.x - 5.0f, panelPos.y + contentHeight - 6.0f);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddLine(dividerStart, dividerEnd, ImGui::GetColorU32(ImGuiCol_Border), 1.0f);

            ImGui::SetCursorScreenPos(ImVec2(panelPos.x - 8.0f, panelPos.y));
            ImGui::InvisibleButton("RightPanelResizer", ImVec2(10.0f, contentHeight - 6.0f));

            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                float delta = ImGui::GetIO().MouseDelta.x;
                rightPanelWidth = std::max(250.0f, std::min(600.0f, rightPanelWidth - delta));
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
        }
    }

    #if !TARGET_OS_IOS && !TARGET_OS_TV
    // File dialogs
    ImVec2 dialogSize = ImVec2(800, 600);

    auto saveCompositeImage = [&](const std::string& filename, bool asPng) -> bool {
        std::vector<pelpaint::Pixel> composite;
        CompositeLayers(composite);

        ImageView view;
        view.data = reinterpret_cast<const std::uint8_t*>(composite.data());
        view.width = static_cast<std::uint32_t>(canvasWidth);
        view.height = static_cast<std::uint32_t>(canvasHeight);
        view.stride = view.width * 4;
        view.channels = 4;

        return asPng
            ? ImageExporter::SaveToPNG(filename, view)
            : ImageExporter::SaveToTGA(filename, view);
    };

    if (ImGuiFileDialog::Instance()->Display("SaveTGADialog", ImGuiWindowFlags_NoCollapse, dialogSize, dialogSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            const std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
            if (saveCompositeImage(path, false)) {
                fs::path p(path);
                SaveLastDirectory(p.parent_path().string());
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("SavePNGDialog", ImGuiWindowFlags_NoCollapse, dialogSize, dialogSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            const std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
            if (saveCompositeImage(path, true)) {
                fs::path p(path);
                SaveLastDirectory(p.parent_path().string());
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("SaveSVGPixelDialog", ImGuiWindowFlags_NoCollapse, dialogSize, dialogSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            SaveToSVGPixel(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("SaveSVGVectorDialog", ImGuiWindowFlags_NoCollapse, dialogSize, dialogSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            SaveToSVGVector(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("SaveDepthMapDialog", ImGuiWindowFlags_NoCollapse, dialogSize, dialogSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            SaveDepthMap(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("SaveMeshDialog", ImGuiWindowFlags_NoCollapse, dialogSize, dialogSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            SaveMesh(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("LoadImageDialog", ImGuiWindowFlags_NoCollapse, dialogSize, dialogSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            LoadFromImage(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }
#endif

    // BOTTOM STATUS BAR (sticky)
    ImGui::SetCursorPos(ImVec2(0, screenSize.y - statusBarHeight - bottomPadding));
    ImGui::BeginChild("StatusBar", ImVec2(0, statusBarHeight), true, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
        DrawStatusBar();
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace pelpaint
