#include "PixelPaintView.hpp"
#include "ImGuiFileDialog.h"
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

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#if defined(USE_METAL_BACKEND)
    #import <Metal/Metal.h>
    #import <QuartzCore/CAMetalLayer.h>
#elif defined(__EMSCRIPTEN__)
    #include <SDL3/SDL_opengles2.h>
#else
    #include <SDL3/SDL_opengl.h>
#endif

#ifdef __APPLE__
    #include <TargetConditionals.h>
    #if TARGET_OS_IOS || TARGET_OS_TV
        #include "IOSFileManager.h"
    #endif
#endif

namespace fs = std::filesystem;

// Constructor
PixelPaintView::PixelPaintView()
    : canvasData(canvasWidth * canvasHeight, Pixel(30, 30, 30, 255))
    , canvasSize(static_cast<float>(canvasWidth), static_cast<float>(canvasHeight))
{
    availablePalettes = ColorPalettes::GetAllPalettes();
    currentFilename = GetDefaultFilename("png");
    this->grayscaleToMono = false; // Shared parameter for all dithering methods

    InitializeTexture();
    PushUndo("Initial state");
    // End of constructor
}

void PixelPaintView::AddSlider(const std::string& label, int min, int max, int step, const std::function<void(int)>& callback) {
    // Persistent slider backed by `sliderValues[label]`.
    // Initialize to `min` when the slider is first created.
    auto it = sliderValues.find(label);
    if (it == sliderValues.end()) {
        it = sliderValues.emplace(label, min).first;
    }

    int value = it->second;
    // Use ImGui slider; `step` is not directly supported by SliderInt,
    // but the slider will still constrain to [min,max]. If `step` is important
    // we could implement +/- buttons; keep simple for now.
    if (ImGui::SliderInt(label.c_str(), &value, min, max)) {
        // Value changed by user
        it->second = value;
        if (callback) callback(value);
    } else {
        // Keep stored value in sync (covers external changes)
        it->second = value;
    }
}

// Destructor
PixelPaintView::~PixelPaintView()
{
    DestroyTexture();
}

// Initialize texture
void PixelPaintView::InitializeTexture()
{
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


void PixelPaintView::SetupDitheringUI()
{
    // Add a shared checkbox for grayscale-to-mono
    AddCheckbox("Grayscale to Mono", [&](bool checked) {
        grayscaleToMono = checked;
    });

    // Add sliders for matrix distance
    AddSlider("Atkinson Matrix Distance", 1, 10, 1, [&](int value) {
        atkinsonMatrixDistance = value;
    });

    AddSlider("Stucki Matrix Distance", 1, 10, 1, [&](int value) {
        stuckiMatrixDistance = value;
    });

    // Add buttons for each dithering method
    AddButton("Apply Atkinson Dithering", [this]() {
        if (selectedPaletteIndex >= 0 && selectedPaletteIndex < availablePalettes.size()) {
            ApplyDithering(DitheringType::Atkinson, availablePalettes[selectedPaletteIndex].colors);
        }
    });

    AddButton("Apply Stucki Dithering", [this]() {
        if (selectedPaletteIndex >= 0 && selectedPaletteIndex < availablePalettes.size()) {
            ApplyDithering(DitheringType::Stucki, availablePalettes[selectedPaletteIndex].colors);
        }
    });

    AddButton("Apply Floyd-Steinberg Dithering", [this]() {
        if (selectedPaletteIndex >= 0 && selectedPaletteIndex < availablePalettes.size()) {
            ApplyDithering(DitheringType::FloydSteinberg, availablePalettes[selectedPaletteIndex].colors);
        }
    });

    AddButton("Apply Ordered Dithering", [this]() {
        if (selectedPaletteIndex >= 0 && selectedPaletteIndex < availablePalettes.size()) {
            ApplyDithering(DitheringType::Ordered, availablePalettes[selectedPaletteIndex].colors);
        }
    });

}

#if defined(USE_METAL_BACKEND)
void PixelPaintView::SetMetalDevice(void* device)
{
    metalDevice = device;
    textureNeedsUpdate = true;
}
#endif

// Update texture from canvas data
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

        if (metalTexture != nullptr) {
            id<MTLTexture> oldTexture = (id<MTLTexture>)metalTexture;
        }

        MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:canvasWidth
            height:canvasHeight
            mipmapped:NO];
        textureDescriptor.usage = MTLTextureUsageShaderRead;

        id<MTLTexture> texture = [device newTextureWithDescriptor:textureDescriptor];

        MTLRegion region = MTLRegionMake2D(0, 0, canvasWidth, canvasHeight);
        [texture replaceRegion:region mipmapLevel:0 withBytes:canvasData.data() bytesPerRow:canvasWidth * 4];

        metalTexture = (void*)texture;
        textureNeedsUpdate = false;
    }
#else
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvasWidth, canvasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, canvasData.data());
    textureNeedsUpdate = false;
#endif
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
    if (newWidth <= 0 || newHeight <= 0) return;

    std::vector<Pixel> newData(newWidth * newHeight, Pixel(30, 30, 30, 255));

    int copyWidth = std::min(canvasWidth, newWidth);
    int copyHeight = std::min(canvasHeight, newHeight);

    for (int y = 0; y < copyHeight; ++y) {
        for (int x = 0; x < copyWidth; ++x) {
            newData[y * newWidth + x] = canvasData[y * canvasWidth + x];
        }
    }

    canvasWidth = newWidth;
    canvasHeight = newHeight;
    canvasData = newData;
    canvasSize = ImVec2(static_cast<float>(canvasWidth), static_cast<float>(canvasHeight));
    textureNeedsUpdate = true;
    PushUndo("Resize canvas");
}

// Clear canvas
void PixelPaintView::ClearCanvas(const Pixel& color)
{
    std::fill(canvasData.begin(), canvasData.end(), color);
    textureNeedsUpdate = true;
    PushUndo("Clear canvas");
}

// Put pixel
void PixelPaintView::PutPixel(int x, int y, const Pixel& color)
{
    if (!IsValidCoord(x, y)) return;

    int index = GetPixelIndex(x, y);
    Pixel& pixel = canvasData[index];

    if (color.a == 255) {
        pixel = color;
    } else {
        float alpha = color.a / 255.0f;
        pixel.r = static_cast<uint8_t>(color.r * alpha + pixel.r * (1 - alpha));
        pixel.g = static_cast<uint8_t>(color.g * alpha + pixel.g * (1 - alpha));
        pixel.b = static_cast<uint8_t>(color.b * alpha + pixel.b * (1 - alpha));
        pixel.a = std::min(255, static_cast<int>(pixel.a) + static_cast<int>(color.a));
    }

    textureNeedsUpdate = true;
}

// Get pixel
Pixel PixelPaintView::GetPixel(int x, int y) const
{
    if (!IsValidCoord(x, y)) return Pixel(0, 0, 0, 0);
    return canvasData[GetPixelIndex(x, y)];
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
void PixelPaintView::DrawLineBresenham(int x0, int y0, int x1, int y1, const Pixel& color, float brushSize)
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
void PixelPaintView::DrawCircle(int cx, int cy, int radius, const Pixel& color, bool filled)
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
void PixelPaintView::FloodFill(int x, int y, const Pixel& fillColor)
{
    if (!IsValidCoord(x, y)) return;

    Pixel targetColor = GetPixel(x, y);
    if (targetColor.r == fillColor.r && targetColor.g == fillColor.g &&
        targetColor.b == fillColor.b && targetColor.a == fillColor.a) {
        return;
    }

    std::queue<std::pair<int, int>> queue;
    queue.push({x, y});

    while (!queue.empty()) {
        auto [cx, cy] = queue.front();
        queue.pop();

        if (!IsValidCoord(cx, cy)) continue;

        Pixel currentColor = GetPixel(cx, cy);
        if (currentColor.r != targetColor.r || currentColor.g != targetColor.g ||
            currentColor.b != targetColor.b || currentColor.a != targetColor.a) {
            continue;
        }

        PutPixel(cx, cy, fillColor);

        queue.push({cx + 1, cy});
        queue.push({cx - 1, cy});
        queue.push({cx, cy + 1});
        queue.push({cx, cy - 1});
    }
}

// Flood fill with threshold
void PixelPaintView::FloodFillWithThreshold(int x, int y, const Pixel& fillColor, float threshold)
{
    if (!IsValidCoord(x, y)) return;

    Pixel targetColor = GetPixel(x, y);
    std::queue<std::pair<int, int>> queue;
    std::vector<bool> visited(canvasWidth * canvasHeight, false);

    queue.push({x, y});
    visited[GetPixelIndex(x, y)] = true;

    while (!queue.empty()) {
        auto [cx, cy] = queue.front();
        queue.pop();

        PutPixel(cx, cy, fillColor);

        int dx[] = {1, -1, 0, 0};
        int dy[] = {0, 0, 1, -1};

        for (int i = 0; i < 4; ++i) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];

            if (!IsValidCoord(nx, ny)) continue;
            if (visited[GetPixelIndex(nx, ny)]) continue;

            Pixel neighborColor = GetPixel(nx, ny);
            float distance = ColorDistance(targetColor, neighborColor);

            if (distance <= threshold) {
                visited[GetPixelIndex(nx, ny)] = true;
                queue.push({nx, ny});
            }
        }
    }
}

// Spray tool
void PixelPaintView::DrawSpray(int x, int y, float radius, const Pixel& color, float density)
{
    int numDots = static_cast<int>(radius * radius * density);

    for (int i = 0; i < numDots; ++i) {
        float angle = static_cast<float>(rand()) / RAND_MAX * 2.0f * 3.14159f;
        float dist = static_cast<float>(rand()) / RAND_MAX * radius;

        int px = x + static_cast<int>(std::cos(angle) * dist);
        int py = y + static_cast<int>(std::sin(angle) * dist);

        PutPixel(px, py, color);
    }
}

// Color distance for dithering and flood fill
float PixelPaintView::ColorDistance(const Pixel& c1, const Pixel& c2) const
{
    int dr = static_cast<int>(c1.r) - static_cast<int>(c2.r);
    int dg = static_cast<int>(c1.g) - static_cast<int>(c2.g);
    int db = static_cast<int>(c1.b) - static_cast<int>(c2.b);
    int da = static_cast<int>(c1.a) - static_cast<int>(c2.a);

    return std::sqrt(dr * dr + dg * dg + db * db + da * da);
}

// Find nearest palette color
Pixel PixelPaintView::FindNearestPaletteColor(const Pixel& color, const std::vector<Pixel>& palette) const
{
    if (palette.empty()) return color;

    Pixel nearest = palette[0];
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
    // Spread the error to neighboring pixels
    for (int dy = -spreadY; dy <= spreadY; ++dy) {
        for (int dx = -spreadX; dx <= spreadX; ++dx) {
            if (dx == 0 && dy == 0) continue; // Skip the current pixel

            int nx = x + dx;
            int ny = y + dy;

            if (IsValidCoord(nx, ny)) {
                Pixel& neighbor = canvasData[GetPixelIndex(nx, ny)];
                // Apply clamped error diffusion to avoid overflow/underflow
                neighbor.r = static_cast<uint8_t>(std::clamp(static_cast<int>(neighbor.r) + (errorR * divisor) / totalWeight, 0, 255));
                neighbor.g = static_cast<uint8_t>(std::clamp(static_cast<int>(neighbor.g) + (errorG * divisor) / totalWeight, 0, 255));
                neighbor.b = static_cast<uint8_t>(std::clamp(static_cast<int>(neighbor.b) + (errorB * divisor) / totalWeight, 0, 255));
            }
        }
    }
}


// Apply palette
void PixelPaintView::ApplyPalette(const std::vector<Pixel>& palette)
{
    for (auto& pixel : canvasData) {
        pixel = FindNearestPaletteColor(pixel, palette);
    }
    textureNeedsUpdate = true;
    PushUndo("Apply palette");
}

// Floyd-Steinberg dithering
void PixelPaintView::ApplyFloydSteinbergDithering(const std::vector<Pixel>& palette)
{
    std::vector<Pixel> tempData = canvasData;

    for (int y = 0; y < canvasHeight; ++y) {
        for (int x = 0; x < canvasWidth; ++x) {
            Pixel oldPixel = tempData[GetPixelIndex(x, y)];
            Pixel newPixel = FindNearestPaletteColor(oldPixel, palette);

            if (ditheringPreserveAlpha) {
                newPixel.a = oldPixel.a;
            }

            tempData[GetPixelIndex(x, y)] = newPixel;

            int errorR = static_cast<int>(oldPixel.r) - static_cast<int>(newPixel.r);
            int errorG = static_cast<int>(oldPixel.g) - static_cast<int>(newPixel.g);
            int errorB = static_cast<int>(oldPixel.b) - static_cast<int>(newPixel.b);

            if (x + 1 < canvasWidth) {
                Pixel& neighbor = tempData[GetPixelIndex(x + 1, y)];
                neighbor.r = std::clamp(static_cast<int>(neighbor.r) + errorR * 7 / 16, 0, 255);
                neighbor.g = std::clamp(static_cast<int>(neighbor.g) + errorG * 7 / 16, 0, 255);
                neighbor.b = std::clamp(static_cast<int>(neighbor.b) + errorB * 7 / 16, 0, 255);
            }

            if (y + 1 < canvasHeight) {
                if (x - 1 >= 0) {
                    Pixel& neighbor = tempData[GetPixelIndex(x - 1, y + 1)];
                    neighbor.r = std::clamp(static_cast<int>(neighbor.r) + errorR * 3 / 16, 0, 255);
                    neighbor.g = std::clamp(static_cast<int>(neighbor.g) + errorG * 3 / 16, 0, 255);
                    neighbor.b = std::clamp(static_cast<int>(neighbor.b) + errorB * 3 / 16, 0, 255);
                }

                Pixel& neighbor = tempData[GetPixelIndex(x, y + 1)];
                neighbor.r = std::clamp(static_cast<int>(neighbor.r) + errorR * 5 / 16, 0, 255);
                neighbor.g = std::clamp(static_cast<int>(neighbor.g) + errorG * 5 / 16, 0, 255);
                neighbor.b = std::clamp(static_cast<int>(neighbor.b) + errorB * 5 / 16, 0, 255);

                if (x + 1 < canvasWidth) {
                    Pixel& neighbor = tempData[GetPixelIndex(x + 1, y + 1)];
                    neighbor.r = std::clamp(static_cast<int>(neighbor.r) + errorR * 1 / 16, 0, 255);
                    neighbor.g = std::clamp(static_cast<int>(neighbor.g) + errorG * 1 / 16, 0, 255);
                    neighbor.b = std::clamp(static_cast<int>(neighbor.b) + errorB * 1 / 16, 0, 255);
                }
            }
        }
    }

    canvasData = tempData;
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
    // Stub implementation for ConvertToGrayscale
    for (auto& pixel : canvasData) {
        int gray = static_cast<int>(0.299 * pixel.r + 0.587 * pixel.g + 0.114 * pixel.b);
        pixel.r = pixel.g = pixel.b = gray;
    }
}


void PixelPaintView::ApplyAtkinsonDithering(const std::vector<Pixel>& palette)
{

    // Iterate over each pixel in the canvas
    for (int y = 0; y < canvasHeight; ++y) {
        for (int x = 0; x < canvasWidth; ++x) {
            Pixel& currentPixel = canvasData[GetPixelIndex(x, y)];
            Pixel closestColor = FindNearestPaletteColor(currentPixel, palette);

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
    textureNeedsUpdate = true;
}



void PixelPaintView::ApplyStuckiDithering(const std::vector<Pixel>& palette)
{
    // Iterate over each pixel in the canvas
    for (int y = 0; y < canvasHeight; ++y) {
        for (int x = 0; x < canvasWidth; ++x) {
            Pixel& currentPixel = canvasData[GetPixelIndex(x, y)];
            Pixel closestColor = FindNearestPaletteColor(currentPixel, palette);

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
    textureNeedsUpdate = true;
}



// Ordered dithering
void PixelPaintView::ApplyOrderedDithering(const std::vector<Pixel>& palette)
{
    const int ditherPatternSize = 4;
    const int ditherPattern[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5}
    };

    for (int y = 0; y < canvasHeight; ++y) {
        for (int x = 0; x < canvasWidth; ++x) {
            Pixel pixel = canvasData[GetPixelIndex(x, y)];
            int ditherValue = ditherPattern[y % ditherPatternSize][x % ditherPatternSize];

            Pixel ditheredPixel;
            ditheredPixel.r = std::clamp(static_cast<int>(pixel.r) + ditherValue - 8, 0, 255);
            ditheredPixel.g = std::clamp(static_cast<int>(pixel.g) + ditherValue - 8, 0, 255);
            ditheredPixel.b = std::clamp(static_cast<int>(pixel.b) + ditherValue - 8, 0, 255);
            ditheredPixel.a = pixel.a;

            Pixel quantized = FindNearestPaletteColor(ditheredPixel, palette);

            if (ditheringPreserveAlpha) {
                quantized.a = pixel.a;
            }

            canvasData[GetPixelIndex(x, y)] = quantized;
        }
    }

    textureNeedsUpdate = true;
    PushUndo("Apply ordered dithering");

}


// Undo/Redo
void PixelPaintView::PushUndo(const std::string& description)
{
    if (undoStack.size() >= maxUndoSteps) {
        undoStack.erase(undoStack.begin());
    }
    undoStack.emplace_back(canvasData, description);
    redoStack.clear();
}

void PixelPaintView::Undo()
{
    if (undoStack.size() <= 1) return;

    redoStack.push_back(undoStack.back());
    undoStack.pop_back();
    canvasData = undoStack.back().data;
    textureNeedsUpdate = true;
}

void PixelPaintView::Redo()
{
    if (redoStack.empty()) return;

    undoStack.push_back(redoStack.back());
    canvasData = redoStack.back().data;
    redoStack.pop_back();
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

    for (const auto& pixel : canvasData) {
        fputc(pixel.b, file);
        fputc(pixel.g, file);
        fputc(pixel.r, file);
        fputc(pixel.a, file);
    }

    fclose(file);
    return true;
}

bool PixelPaintView::SaveToPNG(const std::string& filename)
{
    std::vector<uint8_t> pngData;
    pngData.reserve(canvasWidth * canvasHeight * 4);

    for (const auto& pixel : canvasData) {
        pngData.push_back(pixel.r);
        pngData.push_back(pixel.g);
        pngData.push_back(pixel.b);
        pngData.push_back(pixel.a);
    }

    return stbi_write_png(filename.c_str(), canvasWidth, canvasHeight, 4, pngData.data(), canvasWidth * 4) != 0;
}

bool PixelPaintView::SaveToJPEG(const std::string& filename, int quality)
{
    std::vector<uint8_t> jpegData;
    jpegData.reserve(canvasWidth * canvasHeight * 3);

    for (const auto& pixel : canvasData) {
        jpegData.push_back(pixel.r);
        jpegData.push_back(pixel.g);
        jpegData.push_back(pixel.b);
    }

    return stbi_write_jpg(filename.c_str(), canvasWidth, canvasHeight, 3, jpegData.data(), quality) != 0;
}

bool PixelPaintView::LoadFromImage(const std::string& filename)
{
    int width, height, channels;
    unsigned char* imageData = stbi_load(filename.c_str(), &width, &height, &channels, 4);

    if (!imageData) return false;

    ResizeCanvas(width, height);

    for (int i = 0; i < width * height; ++i) {
        canvasData[i].r = imageData[i * 4];
        canvasData[i].g = imageData[i * 4 + 1];
        canvasData[i].b = imageData[i * 4 + 2];
        canvasData[i].a = imageData[i * 4 + 3];
    }

    stbi_image_free(imageData);
    textureNeedsUpdate = true;
    SetFilenameFromLoadedImage(filename);
    PushUndo("Load image");
    return true;
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
        frequentColors.push_back(Pixel(
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

    currentSelection.width = x2 - x1 + 1;
    currentSelection.height = y2 - y1 + 1;
    currentSelection.selectionStart = ImVec2(static_cast<float>(x1), static_cast<float>(y1));
    currentSelection.selectionEnd = ImVec2(static_cast<float>(x2), static_cast<float>(y2));
    currentSelection.sourceCenter = ImVec2(
        (startPoint.x + endPoint.x) / 2.0f,
        (startPoint.y + endPoint.y) / 2.0f
    );

    currentSelection.pixels.clear();
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
                currentSelection.pixels.push_back(GetPixel(x, y));
            }
        }
    }

    currentSelection.isActive = true;
}

// Paste selection
void PixelPaintView::PasteSelection(const ImVec2& pastePos)
{
    if (!currentSelection.isActive || currentSelection.pixels.empty()) return;

    int startX = static_cast<int>(pastePos.x);
    int startY = static_cast<int>(pastePos.y);

    size_t pixelIndex = 0;
    int centerX = (static_cast<int>(currentSelection.selectionStart.x) + static_cast<int>(currentSelection.selectionEnd.x)) / 2;
    int centerY = (static_cast<int>(currentSelection.selectionStart.y) + static_cast<int>(currentSelection.selectionEnd.y)) / 2;
    int radiusX = (static_cast<int>(currentSelection.selectionEnd.x) - static_cast<int>(currentSelection.selectionStart.x)) / 2;
    int radiusY = (static_cast<int>(currentSelection.selectionEnd.y) - static_cast<int>(currentSelection.selectionStart.y)) / 2;

    for (int y = 0; y < currentSelection.height; ++y) {
        for (int x = 0; x < currentSelection.width; ++x) {
            int canvasX = startX + x;
            int canvasY = startY + y;

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

    std::vector<Pixel> blurredData = canvasData;
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
                        Pixel p = canvasData[GetPixelIndex(nx, ny)];
                        r += p.r;
                        g += p.g;
                        b += p.b;
                        a += p.a;
                        count++;
                    }
                }
            }

            if (count > 0) {
                blurredData[GetPixelIndex(x, y)] = Pixel(
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

    if (ImGui::IsKeyPressed(ImGuiKey_1)) currentTool = DrawTool::Pencil;
    if (ImGui::IsKeyPressed(ImGuiKey_2)) currentTool = DrawTool::Eraser;
    if (ImGui::IsKeyPressed(ImGuiKey_3)) currentTool = DrawTool::Line;
    if (ImGui::IsKeyPressed(ImGuiKey_4)) currentTool = DrawTool::Fill;
    if (ImGui::IsKeyPressed(ImGuiKey_5)) currentTool = DrawTool::Eyedropper;
    if (ImGui::IsKeyPressed(ImGuiKey_6)) currentTool = DrawTool::Spray;
    if (ImGui::IsKeyPressed(ImGuiKey_7)) currentTool = DrawTool::RectangleSelect;
    if (ImGui::IsKeyPressed(ImGuiKey_8)) currentTool = DrawTool::CircleSelect;
    if (ImGui::IsKeyPressed(ImGuiKey_9)) currentTool = DrawTool::BucketFill;
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
                if (bucketThreshold > 0.0f) {
                    FloodFillWithThreshold(pixelX, pixelY, currentColor, bucketThreshold);
                } else {
                    FloodFill(pixelX, pixelY, currentColor);
                }
                PushUndo("Bucket Fill");
            }
        } else {
            PushUndo("Draw");
        }
    }

    if (isDrawing && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (currentTool == DrawTool::Pencil || currentTool == DrawTool::Eraser) {
            Pixel drawColor = currentColor;
            if (currentTool == DrawTool::Eraser) {
                drawColor = eraserColor;
                if (eraserUseAlpha) {
                    drawColor.a = 0;
                }
            }

            int radius = static_cast<int>(brushSettings.size / 2.0f);
            for (int y = pixelY - radius; y <= pixelY + radius; ++y) {
                for (int x = pixelX - radius; x <= pixelX + radius; ++x) {
                    if ((x - pixelX) * (x - pixelX) + (y - pixelY) * (y - pixelY) <= radius * radius) {
                        PutPixel(x, y, drawColor);
                    }
                }
            }
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
                            Pixel sourcePixel = GetPixel(sourcePixelX, sourcePixelY);
                            PutPixel(pixelX + dx, pixelY + dy, sourcePixel);
                        }
                    }
                }
            }
        } else if (currentTool == DrawTool::RectangleSelect || currentTool == DrawTool::CircleSelect) {
            CopySelection(ImVec2(static_cast<float>(lastDrawPoint.x), static_cast<float>(lastDrawPoint.y)), canvasMousePos, currentTool == DrawTool::CircleSelect);
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (currentTool == DrawTool::Line && isLineMode) {
            Pixel drawColor = currentColor;
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
    const char* toolLetters[] = { "P", "E", "L", "F", "I", "S", "R", "C", "K" };
    const char* toolNames[] = {
        "Pencil",
        "Eraser",
        "Line",
        "Fill",
        "Eyedropper",
        "Spray",
        "Rect Select",
        "Circle Select",
        "Bucket Fill"
    };

    ImGui::BeginGroup();
    {
        float buttonSize = 40.0f;
        int toolCount = sizeof(toolLetters) / sizeof(toolLetters[0]);

        for (int i = 0; i < toolCount; ++i) {
            DrawTool tool = static_cast<DrawTool>(i);
            bool selected = (currentTool == tool);

            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
            }

            if (ImGui::Button(toolLetters[i], ImVec2(buttonSize, buttonSize))) {
                currentTool = tool;
                if (tool == DrawTool::Clone) {
                    cloneSourceSet = false;
                }
            }

            if (selected) {
                ImGui::PopStyleColor();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", toolNames[i]);
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

void PixelPaintView::ApplyDithering(DitheringType type, const std::vector<Pixel>& palette)
{
    switch (type) {
        case DitheringType::Atkinson:
            if (atkinsonGrayscaleToMono) {
                ConvertToGrayscale();
            }
            ApplyAtkinsonDithering(palette);
            break;

        case DitheringType::FloydSteinberg:
            if (grayscaleToMono) {
                ConvertToGrayscale();
            }
            ApplyFloydSteinbergDithering(palette);
            break;

        case DitheringType::Ordered:
            if (grayscaleToMono) {
                ConvertToGrayscale();
            }
            ApplyOrderedDithering(palette);
            break;

        case DitheringType::Stucki:
            if (stuckiGrayscaleToMono) {
                ConvertToGrayscale();
            }
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
    ImGui::Checkbox("Enable Dithering", &ditheringEnabled);
    if (ditheringEnabled) {
        ImGui::Checkbox("Preserve Alpha", &ditheringPreserveAlpha);
        if (ImGui::Button("Apply Direct", ImVec2(-1, 0))) {
            if (!customPalette.empty()) {
                ApplyPalette(customPalette);
            }
        }
        // Use the unified dithering UI setup
        SetupDitheringUI();
    }
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
        ImGui::SliderFloat("Size##slider", &brushSettings.size, 1.0f, 100.0f);
    }

    ImGui::SliderFloat("Opacity##brush", &brushSettings.opacity, 0.0f, 1.0f);
    ImGui::Checkbox("Antialiased", &brushSettings.antialiased);

    ImGui::Separator();
    ImGui::Text("Eraser Settings");
    ImGui::Checkbox("Use Alpha", &eraserUseAlpha);

    if (currentTool == DrawTool::BucketFill) {
        ImGui::Separator();
        ImGui::Text("Bucket Fill");
        ImGui::SliderFloat("Threshold##bucket", &bucketThreshold, 0.0f, 255.0f);
    }
}

// Draw canvas view
void PixelPaintView::DrawCanvasView()
{
    UpdateTexture();

    ImVec2 imageSize = ImVec2(canvasWidth * canvasScale, canvasHeight * canvasScale);
    canvasPos = ImGui::GetCursorScreenPos();

#if defined(USE_METAL_BACKEND)
    if (metalTexture != nullptr) {
        ImGui::Image(metalTexture, imageSize);
    }
#else
    ImGui::Image(reinterpret_cast<void*>(static_cast<intptr_t>(textureID)), imageSize, ImVec2(0, 1), ImVec2(1, 0));
#endif

    HandleCanvasInput();
    DrawSelectionOverlay();

    if (showGrid) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        for (int x = 0; x < canvasWidth; x += gridSize) {
            float screenX = canvasPos.x + x * canvasScale;
            drawList->AddLine(
                ImVec2(screenX, canvasPos.y),
                ImVec2(screenX, canvasPos.y + canvasHeight * canvasScale),
                ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.2f))
            );
        }
        for (int y = 0; y < canvasHeight; y += gridSize) {
            float screenY = canvasPos.y + y * canvasScale;
            drawList->AddLine(
                ImVec2(canvasPos.x, screenY),
                ImVec2(canvasPos.x + canvasWidth * canvasScale, screenY),
                ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.2f))
            );
        }
    }
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

    // Draw bright dashed border
    float dashSize = 4.0f;
    float gapSize = 4.0f;
    ImU32 borderColor = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
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
    }

    // Status text on the left
    ImGui::Text("Tool: %s | Brush: %.1f | Zoom: %.1fx | Color: #%02X%02X%02X%02X",
        toolName,
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




// Main Draw function - NEW LAYOUT
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
    const float leftToolbarWidth = 50.0f;
    const float rightPanelWidth = 260.0f;

    // LEFT TOOLBAR (thin, vertical) - tools only
    ImGui::BeginChild("LeftToolbar", ImVec2(leftToolbarWidth, -statusBarHeight), true, ImGuiWindowFlags_NoScrollbar);
    {
        DrawToolbar();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // CENTER CANVAS AREA - main drawing space
    float canvasAreaWidth = screenSize.x - leftToolbarWidth - (rightPanelCollapsed ? 0.0f : rightPanelWidth) - 6.0f;
    ImGui::BeginChild("CanvasArea", ImVec2(canvasAreaWidth, -statusBarHeight), true, ImGuiWindowFlags_NoScrollbar);
    {
        DrawCanvasView();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // RIGHT PANEL - collapsible attributes panel
    if (!rightPanelCollapsed) {
        ImGui::BeginChild("RightPanel", ImVec2(rightPanelWidth, -statusBarHeight), true, ImGuiWindowFlags_NoScrollbar);
        {
            // Edit section - Undo/Redo/Clear
            if (ImGui::CollapsingHeader("Edit", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button("Undo (Ctrl+Z)", ImVec2(-1, 0))) { Undo(); }
                if (ImGui::Button("Redo (Ctrl+Y)", ImVec2(-1, 0))) { Redo(); }
                ImGui::Separator();
                if (ImGui::Button("Clear Canvas", ImVec2(-1, 0))) { ClearCanvas(); }
            }
            ImGui::Spacing();

            // Color & Palette section
            if (ImGui::CollapsingHeader("Color & Palette", ImGuiTreeNodeFlags_DefaultOpen)) {
                DrawColorPicker();
                ImGui::Spacing();
                DrawPaletteSelector();
            }
            ImGui::Spacing();

            // Brush Settings section
            if (ImGui::CollapsingHeader("Brush", ImGuiTreeNodeFlags_DefaultOpen)) {
                DrawBrushSettings();
            }
            ImGui::Spacing();

            // Recent Colors section
            if (ImGui::CollapsingHeader("Recent Colors")) {
                if (currentTool == DrawTool::Pencil || currentTool == DrawTool::Eraser) {
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
            ImGui::Spacing();

            // View section - WITH ZOOM CONTROL
            if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Zoom##zoom", &canvasScale, 0.1f, 20.0f, "%.1fx");
                ImGui::Separator();
                ImGui::Checkbox("Show Grid", &showGrid);
                if (showGrid) {
                    ImGui::SliderInt("Grid Size", &gridSize, 2, 64);
                }
            }
            ImGui::Spacing();

            // File Operations section
            if (ImGui::CollapsingHeader("Files")) {
                // Show current default filename
                ImGui::Text("Filename: %s", currentFilename.c_str());
                ImGui::Spacing();

#if TARGET_OS_IOS || TARGET_OS_TV
                if (ImGui::Button("Share TGA", ImVec2(-1, 0))) {
                    SaveToTGA(currentFilename + ".tga");
                }
                if (ImGui::Button("Share Binary", ImVec2(-1, 0))) {
                    SaveBinary(currentFilename + ".pxl");
                }
                if (ImGui::Button("Save to Files", ImVec2(-1, 0))) {
                    // iOS share implementation
                }
                if (ImGui::Button("Open File", ImVec2(-1, 0))) {
                    // iOS file picker
                }
#else
                if (ImGui::Button("Save TGA", ImVec2(-1, 0))) {
                    ImGuiFileDialog::Instance()->OpenDialog("SaveTGADialog", "Save TGA", ".tga", ".", 1, nullptr, ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite);
                }
                if (ImGui::Button("Save PNG", ImVec2(-1, 0))) {
                    ImGuiFileDialog::Instance()->OpenDialog("SavePNGDialog", "Save PNG", ".png", ".", 1, nullptr, ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite);
                }
                if (ImGui::Button("Load Image", ImVec2(-1, 0))) {
                    ImGuiFileDialog::Instance()->OpenDialog("LoadImageDialog", "Load Image", ".tga,.png,.jpg,.jpeg", ".", 1, nullptr, ImGuiFileDialogFlags_Modal);
                }
#endif
            }
        }
        ImGui::EndChild();
    }

    #if !TARGET_OS_IOS && !TARGET_OS_TV
    // File dialogs
    ImVec2 dialogSize = ImVec2(800, 600);

    if (ImGuiFileDialog::Instance()->Display("SaveTGADialog", ImGuiWindowFlags_NoCollapse, dialogSize, dialogSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            SaveToTGA(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("SavePNGDialog", ImGuiWindowFlags_NoCollapse, dialogSize, dialogSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            SaveToPNG(ImGuiFileDialog::Instance()->GetFilePathName());
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
    ImGui::SetCursorPos(ImVec2(0, screenSize.y - statusBarHeight));
    ImGui::BeginChild("StatusBar", ImVec2(0, statusBarHeight), true, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
        DrawStatusBar();
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    ImGui::End();
}
