#include "PixelPaintView.hpp"
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <queue>

#if defined(USE_METAL_BACKEND)
    #import <Metal/Metal.h>
    #import <QuartzCore/CAMetalLayer.h>
#elif defined(__EMSCRIPTEN__)
    #include <SDL3/SDL_opengles2.h>
#else
    #include <SDL3/SDL_opengl.h>
#endif

namespace fs = std::filesystem;

// Constructor
PixelPaintView::PixelPaintView()
    : canvasData(canvasWidth * canvasHeight, Pixel(30, 30, 30, 255))
    , canvasSize(static_cast<float>(canvasWidth), static_cast<float>(canvasHeight))
{
    availablePalettes = ColorPalettes::GetAllPalettes();
    InitializeTexture();
    PushUndo("Initial state");
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
    // Metal texture will be created when device is set
    metalTexture = nullptr;
    textureNeedsUpdate = false;  // Don't try to update until device is set
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
// Set Metal device (called from main)
void PixelPaintView::SetMetalDevice(void* device)
{
    // Just store the pointer - main app owns the device lifetime
    metalDevice = device;
    // Trigger texture creation now that we have a device
    textureNeedsUpdate = true;
}
#endif

// Update texture from canvas data
void PixelPaintView::UpdateTexture()
{
    if (!textureNeedsUpdate) return;
    
#if defined(USE_METAL_BACKEND)
    @autoreleasepool {
        // Use cached Metal device (set from main)
        id<MTLDevice> device = (__bridge id<MTLDevice>)metalDevice;
        if (!device) {
            std::cerr << "Metal device not set - call SetMetalDevice() first" << std::endl;
            return;
        }
        
        id<MTLTexture> texture = nil;
        
        // Check if we need to create a new texture (first time or size change)
        bool needsNewTexture = (metalTexture == nullptr);
        
        if (metalTexture != nullptr) {
            texture = (__bridge id<MTLTexture>)metalTexture;
            // Check if size changed
            if (texture.width != canvasWidth || texture.height != canvasHeight) {
                needsNewTexture = true;
                // Release old texture
                (__bridge_transfer id<MTLTexture>)metalTexture;
                metalTexture = nullptr;
                texture = nil;
            }
        }
        
        if (needsNewTexture) {
            // Create new texture
            MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                         width:canvasWidth
                                                                                                        height:canvasHeight
                                                                                                     mipmapped:NO];
            textureDescriptor.usage = MTLTextureUsageShaderRead;
            textureDescriptor.storageMode = MTLStorageModeManaged;
            
            texture = [device newTextureWithDescriptor:textureDescriptor];
            
            if (!texture) {
                std::cerr << "Failed to create Metal texture" << std::endl;
                return;
            }
            
            // Store texture pointer with retained ownership
            metalTexture = (__bridge_retained void*)texture;
        }
        
        // Upload/update pixel data
        MTLRegion region = MTLRegionMake2D(0, 0, canvasWidth, canvasHeight);
        [texture replaceRegion:region
                   mipmapLevel:0
                     withBytes:canvasData.data()
                   bytesPerRow:canvasWidth * 4];
        
        textureNeedsUpdate = false;
    }
#else
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvasWidth, canvasHeight, 
                 0, GL_RGBA, GL_UNSIGNED_BYTE, canvasData.data());
    textureNeedsUpdate = false;
#endif
}

// Destroy texture
void PixelPaintView::DestroyTexture()
{
#if defined(USE_METAL_BACKEND)
    if (metalTexture != nullptr) {
        id<MTLTexture> texture = (__bridge_transfer id<MTLTexture>)metalTexture;
        texture = nil;
        metalTexture = nullptr;
    }
#else
    if (textureID != 0) {
        glDeleteTextures(1, &textureID);
        textureID = 0;
    }
#endif
}

// Check if coordinates are valid
bool PixelPaintView::IsValidCoord(int x, int y) const
{
    return x >= 0 && x < canvasWidth && y >= 0 && y < canvasHeight;
}

// Get pixel index
int PixelPaintView::GetPixelIndex(int x, int y) const
{
    return y * canvasWidth + x;
}

// Put pixel
void PixelPaintView::PutPixel(int x, int y, const Pixel& color)
{
    if (!IsValidCoord(x, y)) return;
    
    int idx = GetPixelIndex(x, y);
    canvasData[idx] = color;
    textureNeedsUpdate = true;
}

// Get pixel
Pixel PixelPaintView::GetPixel(int x, int y) const
{
    if (!IsValidCoord(x, y)) return Pixel(0, 0, 0, 0);
    
    int idx = GetPixelIndex(x, y);
    return canvasData[idx];
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
        // Draw brush at current position
        if (brushSize <= 1.0f) {
            PutPixel(x0, y0, color);
        } else {
            DrawCircle(x0, y0, radius, color, true);
        }
        
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

// Draw circle
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
        // Midpoint circle algorithm
        int x = radius;
        int y = 0;
        int err = 0;
        
        while (x >= y) {
            PutPixel(cx + x, cy + y, color);
            PutPixel(cx + y, cy + x, color);
            PutPixel(cx - y, cy + x, color);
            PutPixel(cx - x, cy + y, color);
            PutPixel(cx - x, cy - y, color);
            PutPixel(cx - y, cy - x, color);
            PutPixel(cx + y, cy - x, color);
            PutPixel(cx + x, cy - y, color);
            
            if (err <= 0) {
                y += 1;
                err += 2 * y + 1;
            }
            if (err > 0) {
                x -= 1;
                err -= 2 * x + 1;
            }
        }
    }
}

// Flood fill
void PixelPaintView::FloodFill(int x, int y, const Pixel& fillColor)
{
    if (!IsValidCoord(x, y)) return;
    
    Pixel targetColor = GetPixel(x, y);
    if (targetColor == fillColor) return;
    
    std::queue<std::pair<int, int>> queue;
    queue.push({x, y});
    
    while (!queue.empty()) {
        auto [cx, cy] = queue.front();
        queue.pop();
        
        if (!IsValidCoord(cx, cy)) continue;
        if (!(GetPixel(cx, cy) == targetColor)) continue;
        
        PutPixel(cx, cy, fillColor);
        
        queue.push({cx + 1, cy});
        queue.push({cx - 1, cy});
        queue.push({cx, cy + 1});
        queue.push({cx, cy - 1});
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

// Apply palette to canvas
void PixelPaintView::ApplyPalette(const std::vector<Pixel>& palette)
{
    if (palette.empty()) return;
    
    PushUndo("Apply palette");
    
    for (int y = 0; y < canvasHeight; ++y) {
        for (int x = 0; x < canvasWidth; ++x) {
            Pixel current = GetPixel(x, y);
            Pixel nearest = Pixel::FindNearest(current, palette);
            PutPixel(x, y, nearest);
        }
    }
    
    textureNeedsUpdate = true;
}

// Floyd-Steinberg dithering
void PixelPaintView::ApplyFloydSteinbergDithering(const std::vector<Pixel>& palette)
{
    if (palette.empty()) return;
    
    PushUndo("Apply Floyd-Steinberg Dithering");
    
    // Work with a temporary buffer to avoid reading already-quantized pixels
    std::vector<Pixel> tempData = canvasData;
    
    // Helper lambda to distribute error to a neighbor pixel
    auto DistributeError = [&](int x, int y, int errR, int errG, int errB, float factor) {
        if (!IsValidCoord(x, y)) return;
        
        int idx = GetPixelIndex(x, y);
        Pixel& p = tempData[idx];
        
        // Add weighted error to neighbor, clamping to valid range
        p.r = std::clamp(static_cast<int>(p.r) + static_cast<int>(errR * factor), 0, 255);
        p.g = std::clamp(static_cast<int>(p.g) + static_cast<int>(errG * factor), 0, 255);
        p.b = std::clamp(static_cast<int>(p.b) + static_cast<int>(errB * factor), 0, 255);
    };
    
    // Process pixels left-to-right, top-to-bottom
    for (int y = 0; y < canvasHeight - 1; ++y) {
        for (int x = 1; x < canvasWidth - 1; ++x) {
            // 1. Get current pixel from temp buffer
            int idx = GetPixelIndex(x, y);
            Pixel oldPixel = tempData[idx];
            
            // 2. Find closest color in palette (Quantize)
            Pixel newPixel = FindNearestPaletteColor(oldPixel, palette);
            
            // 3. Update pixel with quantized color in temp buffer
            tempData[idx] = newPixel;
            
            // 4. Calculate quantization error
            int errR = static_cast<int>(oldPixel.r) - static_cast<int>(newPixel.r);
            int errG = static_cast<int>(oldPixel.g) - static_cast<int>(newPixel.g);
            int errB = static_cast<int>(oldPixel.b) - static_cast<int>(newPixel.b);
            
            // 5. Distribute error to neighboring pixels using Floyd-Steinberg matrix:
            //        X   7/16
            //  3/16 5/16 1/16
            DistributeError(x + 1, y    , errR, errG, errB, 7.0f / 16.0f);  // Right
            DistributeError(x - 1, y + 1, errR, errG, errB, 3.0f / 16.0f);  // Down-Left
            DistributeError(x    , y + 1, errR, errG, errB, 5.0f / 16.0f);  // Down
            DistributeError(x + 1, y + 1, errR, errG, errB, 1.0f / 16.0f);  // Down-Right
        }
    }
    
    // Apply the dithered result
    canvasData = tempData;
    textureNeedsUpdate = true;
}

// Ordered dithering (Bayer matrix)
void PixelPaintView::ApplyOrderedDithering(const std::vector<Pixel>& palette)
{
    if (palette.empty()) return;
    
    PushUndo("Apply ordered dithering");
    
    // 4x4 Bayer matrix
    const float bayerMatrix[4][4] = {
        {0.0f/16.0f, 8.0f/16.0f, 2.0f/16.0f, 10.0f/16.0f},
        {12.0f/16.0f, 4.0f/16.0f, 14.0f/16.0f, 6.0f/16.0f},
        {3.0f/16.0f, 11.0f/16.0f, 1.0f/16.0f, 9.0f/16.0f},
        {15.0f/16.0f, 7.0f/16.0f, 13.0f/16.0f, 5.0f/16.0f}
    };
    
    for (int y = 0; y < canvasHeight; ++y) {
        for (int x = 0; x < canvasWidth; ++x) {
            Pixel current = GetPixel(x, y);
            
            float threshold = bayerMatrix[y % 4][x % 4] - 0.5f;
            float factor = 32.0f; // Dithering strength
            
            Pixel dithered;
            dithered.r = std::clamp(static_cast<int>(current.r) + static_cast<int>(threshold * factor), 0, 255);
            dithered.g = std::clamp(static_cast<int>(current.g) + static_cast<int>(threshold * factor), 0, 255);
            dithered.b = std::clamp(static_cast<int>(current.b) + static_cast<int>(threshold * factor), 0, 255);
            dithered.a = current.a;
            
            Pixel nearest = Pixel::FindNearest(dithered, palette);
            PutPixel(x, y, nearest);
        }
    }
    
    textureNeedsUpdate = true;
}

// Find nearest palette color
Pixel PixelPaintView::FindNearestPaletteColor(const Pixel& color, const std::vector<Pixel>& palette) const
{
    return Pixel::FindNearest(color, palette);
}

// Undo/Redo
void PixelPaintView::PushUndo(const std::string& description)
{
    undoStack.emplace_back(canvasData, description);
    
    if (undoStack.size() > maxUndoSteps) {
        undoStack.erase(undoStack.begin());
    }
    
    redoStack.clear();
}

void PixelPaintView::Undo()
{
    if (undoStack.empty()) return;
    
    redoStack.emplace_back(canvasData, "Redo point");
    
    canvasData = undoStack.back().data;
    undoStack.pop_back();
    
    textureNeedsUpdate = true;
}

void PixelPaintView::Redo()
{
    if (redoStack.empty()) return;
    
    undoStack.emplace_back(canvasData, "Undo point");
    
    canvasData = redoStack.back().data;
    redoStack.pop_back();
    
    textureNeedsUpdate = true;
}

void PixelPaintView::ClearUndoStack()
{
    undoStack.clear();
    redoStack.clear();
}

// Clear canvas
void PixelPaintView::ClearCanvas(const Pixel& color)
{
    PushUndo("Clear canvas");
    std::fill(canvasData.begin(), canvasData.end(), color);
    textureNeedsUpdate = true;
}

// Resize canvas
void PixelPaintView::ResizeCanvas(int newWidth, int newHeight)
{
    if (newWidth <= 0 || newHeight <= 0) return;
    if (newWidth == canvasWidth && newHeight == canvasHeight) return;
    
    PushUndo("Resize canvas");
    
    std::vector<Pixel> newData(newWidth * newHeight, Pixel(0, 0, 0, 255));
    
    int minW = std::min(canvasWidth, newWidth);
    int minH = std::min(canvasHeight, newHeight);
    
    for (int y = 0; y < minH; ++y) {
        for (int x = 0; x < minW; ++x) {
            int oldIdx = y * canvasWidth + x;
            int newIdx = y * newWidth + x;
            newData[newIdx] = canvasData[oldIdx];
        }
    }
    
    canvasData = newData;
    canvasWidth = newWidth;
    canvasHeight = newHeight;
    canvasSize = ImVec2(static_cast<float>(canvasWidth), static_cast<float>(canvasHeight));
    
    DestroyTexture();
    InitializeTexture();
    textureNeedsUpdate = true;
}

// Save to TGA format
bool PixelPaintView::SaveToTGA(const std::string& filename)
{
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        return false;
    }
    
    // TGA Header (18 bytes)
    uint8_t header[18] = {0};
    header[2] = 2; // Uncompressed true-color image
    header[12] = canvasWidth & 0xFF;
    header[13] = (canvasWidth >> 8) & 0xFF;
    header[14] = canvasHeight & 0xFF;
    header[15] = (canvasHeight >> 8) & 0xFF;
    header[16] = 32; // 32 bits per pixel (RGBA)
    header[17] = 0x20; // Top-left origin
    
    file.write(reinterpret_cast<char*>(header), 18);
    
    // Write pixel data (BGRA format for TGA)
    for (int y = canvasHeight - 1; y >= 0; --y) {
        for (int x = 0; x < canvasWidth; ++x) {
            Pixel p = GetPixel(x, y);
            file.put(p.b);
            file.put(p.g);
            file.put(p.r);
            file.put(p.a);
        }
    }
    
    file.close();
    std::cout << "Saved TGA: " << filename << std::endl;
    return true;
}

// Save to PNG (stub - needs stb_image_write or SDL_image)
bool PixelPaintView::SaveToPNG(const std::string& filename)
{
    std::cerr << "PNG export not yet implemented. Use TGA format or implement with stb_image_write." << std::endl;
    return false;
}

// Load from image (stub - needs stb_image or SDL_image)
bool PixelPaintView::LoadFromImage(const std::string& filename)
{
    std::cerr << "Image import not yet implemented. Implement with stb_image or SDL_image." << std::endl;
    return false;
}

// Save binary format
bool PixelPaintView::SaveBinary(const std::string& filename)
{
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    
    file.write(reinterpret_cast<const char*>(&canvasWidth), sizeof(canvasWidth));
    file.write(reinterpret_cast<const char*>(&canvasHeight), sizeof(canvasHeight));
    file.write(reinterpret_cast<const char*>(canvasData.data()), 
               canvasData.size() * sizeof(Pixel));
    
    file.close();
    return true;
}

// Load binary format
bool PixelPaintView::LoadBinary(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    
    int width, height;
    file.read(reinterpret_cast<char*>(&width), sizeof(width));
    file.read(reinterpret_cast<char*>(&height), sizeof(height));
    
    if (width != canvasWidth || height != canvasHeight) {
        ResizeCanvas(width, height);
    }
    
    file.read(reinterpret_cast<char*>(canvasData.data()), 
              canvasData.size() * sizeof(Pixel));
    
    file.close();
    textureNeedsUpdate = true;
    PushUndo("Load image");
    return true;
}

// Screen to canvas coordinate conversion
ImVec2 PixelPaintView::ScreenToCanvas(const ImVec2& screenPos) const
{
    ImVec2 relative = ImVec2(screenPos.x - canvasPos.x, screenPos.y - canvasPos.y);
    return ImVec2(relative.x / canvasScale, relative.y / canvasScale);
}

ImVec2 PixelPaintView::CanvasToScreen(const ImVec2& canvasPos) const
{
    return ImVec2(
        this->canvasPos.x + canvasPos.x * canvasScale,
        this->canvasPos.y + canvasPos.y * canvasScale
    );
}

// Handle canvas input
void PixelPaintView::HandleCanvasInput()
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    
    // Check if mouse is over canvas
    if (!ImGui::IsItemHovered()) {
        isDrawing = false;
        return;
    }
    
    // Get canvas coordinates
    ImVec2 canvasMousePos = ScreenToCanvas(mousePos);
    int pixelX = static_cast<int>(canvasMousePos.x);
    int pixelY = static_cast<int>(canvasMousePos.y);
    
    // Start drawing
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        isDrawing = true;
        lastDrawPoint = ImVec2(static_cast<float>(pixelX), static_cast<float>(pixelY));
        
        if (currentTool == DrawTool::Line) {
            if (!isLineMode) {
                lineStartPoint = lastDrawPoint;
                isLineMode = true;
            } else {
                PushUndo("Draw line");
                DrawLineBresenham(
                    static_cast<int>(lineStartPoint.x), static_cast<int>(lineStartPoint.y),
                    pixelX, pixelY, currentColor, brushSettings.size
                );
                isLineMode = false;
            }
        } else if (currentTool == DrawTool::Fill) {
            PushUndo("Fill");
            FloodFill(pixelX, pixelY, currentColor);
        } else if (currentTool == DrawTool::Eyedropper) {
            if (IsValidCoord(pixelX, pixelY)) {
                currentColor = GetPixel(pixelX, pixelY);
            }
        } else {
            PushUndo("Draw");
        }
    }
    
    // Continue drawing
    if (isDrawing && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (currentTool == DrawTool::Pencil || currentTool == DrawTool::Eraser) {
            Pixel drawColor = currentTool == DrawTool::Eraser ? 
                             Pixel(30, 30, 30, 255) : currentColor;
            
            // Draw line from last point to current point for smooth strokes
            DrawLineBresenham(
                static_cast<int>(lastDrawPoint.x), static_cast<int>(lastDrawPoint.y),
                pixelX, pixelY, drawColor, brushSettings.size
            );
            
            lastDrawPoint = ImVec2(static_cast<float>(pixelX), static_cast<float>(pixelY));
        } else if (currentTool == DrawTool::Spray) {
            DrawSpray(pixelX, pixelY, brushSettings.size * 3.0f, currentColor, 0.3f);
        }
    }
    
    // Stop drawing
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        isDrawing = false;
    }
    
    // Zoom with mouse wheel
    if (io.MouseWheel != 0 && io.KeyCtrl) {
        canvasScale = std::clamp(canvasScale + io.MouseWheel * 0.1f, 0.1f, 10.0f);
    }
}

// Handle keyboard shortcuts
void PixelPaintView::HandleKeyboardShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        Undo();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        Redo();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        // Save trigger (handled in DrawToolbar)
    }
    if (ImGui::IsKeyPressed(ImGuiKey_P)) {
        currentTool = DrawTool::Pencil;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E)) {
        currentTool = DrawTool::Eraser;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_L)) {
        currentTool = DrawTool::Line;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F)) {
        currentTool = DrawTool::Fill;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_I)) {
        currentTool = DrawTool::Eyedropper;
    }
}

// Draw toolbar
void PixelPaintView::DrawToolbar()
{
    const char* toolNames[] = {"Pencil (P)", "Eraser (E)", "Line (L)", "Fill (F)", "Eyedropper (I)", "Spray"};
    
    for (int i = 0; i < 6; ++i) {
        if (i > 0) ImGui::SameLine();
        
        bool selected = (static_cast<int>(currentTool) == i);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
        
        if (ImGui::Button(toolNames[i])) {
            currentTool = static_cast<DrawTool>(i);
            isLineMode = false;
        }
        
        if (selected) ImGui::PopStyleColor();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Undo (Ctrl+Z)")) Undo();
    ImGui::SameLine();
    if (ImGui::Button("Redo (Ctrl+Y)")) Redo();
    ImGui::SameLine();
    if (ImGui::Button("Clear")) ClearCanvas();
}

// Draw color picker
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

// Draw palette selector
void PixelPaintView::DrawPaletteSelector()
{
    ImGui::Separator();
    ImGui::Checkbox("Use Palette", &paletteEnabled);
    
    if (paletteEnabled) {
        ImGui::SameLine();
        ImGui::Checkbox("Live Dithering", &ditheringEnabled);
        
        const char* preview = selectedPaletteIndex >= 0 ? 
            availablePalettes[selectedPaletteIndex].name.c_str() : "None";
        
        if (ImGui::BeginCombo("Palette", preview)) {
            for (size_t i = 0; i < availablePalettes.size(); ++i) {
                bool isSelected = (selectedPaletteIndex == static_cast<int>(i));
                if (ImGui::Selectable(availablePalettes[i].name.c_str(), isSelected)) {
                    selectedPaletteIndex = static_cast<int>(i);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        // Show palette colors
        if (selectedPaletteIndex >= 0) {
            ImGui::Text("Palette Colors:");
            const auto& palette = availablePalettes[selectedPaletteIndex].colors;
            
            for (size_t i = 0; i < palette.size(); ++i) {
                if (i > 0 && i % 16 != 0) ImGui::SameLine();
                
                ImVec4 col = ImVec4(
                    palette[i].r / 255.0f,
                    palette[i].g / 255.0f,
                    palette[i].b / 255.0f,
                    1.0f
                );
                
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::ColorButton("##palettecolor", col, 0, ImVec2(20, 20))) {
                    currentColor = palette[i];
                }
                ImGui::PopID();
            }
            
            ImGui::Separator();
            if (ImGui::Button("Apply Palette")) {
                ApplyPalette(palette);
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply with Dithering")) {
                ApplyFloydSteinbergDithering(palette);
            }
        }
    }
}

// Draw brush settings
void PixelPaintView::DrawBrushSettings()
{
    ImGui::Separator();
    ImGui::SliderFloat("Brush Size", &brushSettings.size, 1.0f, 50.0f);
    ImGui::SliderFloat("Opacity", &brushSettings.opacity, 0.0f, 1.0f);
    
    ImGui::Checkbox("Pressure Sensitivity", &pressureSensitivityEnabled);
    if (pressureSensitivityEnabled) {
        ImGui::Text("Pressure: %.2f", currentPressure);
    }
}

// Draw canvas view
void PixelPaintView::DrawCanvasView()
{
    UpdateTexture();
    
    ImVec2 imageSize = ImVec2(canvasWidth * canvasScale, canvasHeight * canvasScale);
    canvasPos = ImGui::GetCursorScreenPos();
    
    // Draw canvas
#if defined(USE_METAL_BACKEND)
    if (metalTexture != nullptr) {
        ImGui::Image(metalTexture, imageSize);
    }
#else
    ImGui::Image(reinterpret_cast<void*>(static_cast<intptr_t>(textureID)), imageSize);
#endif
    
    // Draw grid overlay
    if (showGrid && canvasScale >= 2.0f) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        for (int x = 0; x <= canvasWidth; x += gridSize) {
            float screenX = canvasPos.x + x * canvasScale;
            drawList->AddLine(
                ImVec2(screenX, canvasPos.y),
                ImVec2(screenX, canvasPos.y + imageSize.y),
                IM_COL32(100, 100, 100, 100)
            );
        }
        
        for (int y = 0; y <= canvasHeight; y += gridSize) {
            float screenY = canvasPos.y + y * canvasScale;
            drawList->AddLine(
                ImVec2(canvasPos.x, screenY),
                ImVec2(canvasPos.x + imageSize.x, screenY),
                IM_COL32(100, 100, 100, 100)
            );
        }
    }
    
    HandleCanvasInput();
}

// Draw status bar
void PixelPaintView::DrawStatusBar()
{
    ImGui::Separator();
    ImGui::Text("Canvas: %dx%d | Zoom: %.1fx | Tool: %s | Undo: %zu | Redo: %zu",
        canvasWidth, canvasHeight, canvasScale,
        currentTool == DrawTool::Pencil ? "Pencil" :
        currentTool == DrawTool::Eraser ? "Eraser" :
        currentTool == DrawTool::Line ? "Line" :
        currentTool == DrawTool::Fill ? "Fill" :
        currentTool == DrawTool::Eyedropper ? "Eyedropper" : "Spray",
        undoStack.size(), redoStack.size()
    );
}

// Main draw function
void PixelPaintView::Draw(std::string_view label)
{
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    
    ImVec2 screenSize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(screenSize);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    
    ImGui::Begin(label.data(), nullptr, windowFlags);
    
    HandleKeyboardShortcuts();
    
    // Toolbar
    DrawToolbar();
    
    ImGui::Separator();
    
    // Left panel with tools and settings
    ImGui::BeginChild("LeftPanel", ImVec2(300, 0), true);
    {
        DrawColorPicker();
        DrawPaletteSelector();
        DrawBrushSettings();
        
        ImGui::Separator();
        ImGui::Checkbox("Show Grid", &showGrid);
        if (showGrid) {
            ImGui::SliderInt("Grid Size", &gridSize, 2, 64);
        }
        
        ImGui::Separator();
        ImGui::Text("File Operations:");
        
        ImGui::InputText("Filename", filenameBuffer, sizeof(filenameBuffer));
        
        if (ImGui::Button("Save TGA")) {
            std::string filename = std::string(filenameBuffer) + ".tga";
            SaveToTGA(filename);
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Save Binary")) {
            std::string filename = std::string(filenameBuffer) + ".pxl";
            SaveBinary(filename);
        }
        
        if (ImGui::Button("Load Binary")) {
            std::string filename = std::string(filenameBuffer) + ".pxl";
            LoadBinary(filename);
        }
    }
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    // Main canvas area
    ImGui::BeginChild("Canvas", ImVec2(0, -30), true);
    {
        DrawCanvasView();
    }
    ImGui::EndChild();
    
    // Status bar
    DrawStatusBar();
    
    ImGui::End();
}