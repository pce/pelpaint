#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <tuple>
#include <vector>
#include <memory>
#include <imgui.h>
#include <implot.h>

#include "FileUtils.hpp"
#include "ColorPalettes.hpp"

#if defined(USE_METAL_BACKEND)
    #ifdef __OBJC__
        @protocol MTLDevice;
    #else
        typedef void* id;
    #endif
#endif

namespace fs = std::filesystem;

// Undo/Redo snapshot
struct CanvasSnapshot {
    std::vector<Pixel> data;
    std::string description;
    
    CanvasSnapshot(const std::vector<Pixel>& d, const std::string& desc = "")
        : data(d), description(desc) {}
};

// Drawing tool types
enum class DrawTool {
    Pencil,
    Eraser,
    Line,
    Fill,
    Eyedropper,
    Spray
};

// Brush settings
struct BrushSettings {
    float size = 1.0f;
    float pressure = 1.0f;  // For Apple Pencil/pressure-sensitive input
    float opacity = 1.0f;
    bool antialiased = false;
};

class PixelPaintView
{
public:
    PixelPaintView();
    ~PixelPaintView();
    
    void Draw(std::string_view label);
    
#if defined(USE_METAL_BACKEND)
    void SetMetalDevice(void* device);  // Call this from main to set the device
#endif

private:
    // Canvas dimensions
    int canvasWidth = 800;
    int canvasHeight = 600;
    
    // Core pixel buffer - the "real" image
    std::vector<Pixel> canvasData;
    
    // GPU texture handle (platform-specific)
    unsigned int textureID = 0;
    bool textureNeedsUpdate = true;
    
#if defined(USE_METAL_BACKEND)
    // Cached Metal device to avoid recreating every frame
    void* metalDevice = nullptr;  // id<MTLDevice> stored as void*
#endif
    
    // Texture management
    void InitializeTexture();
    void UpdateTexture();
    void DestroyTexture();
    
    // Canvas operations
    void ResizeCanvas(int newWidth, int newHeight);
    void ClearCanvas(const Pixel& color = Pixel(0, 0, 0, 255));
    
    // Pixel operations
    void PutPixel(int x, int y, const Pixel& color);
    Pixel GetPixel(int x, int y) const;
    bool IsValidCoord(int x, int y) const;
    
    // Drawing algorithms
    void DrawLineBresenham(int x0, int y0, int x1, int y1, const Pixel& color, float brushSize = 1.0f);
    void DrawCircle(int cx, int cy, int radius, const Pixel& color, bool filled = true);
    void FloodFill(int x, int y, const Pixel& fillColor);
    void DrawSpray(int x, int y, float radius, const Pixel& color, float density = 0.3f);
    
    // Palette & Dithering
    void ApplyPalette(const std::vector<Pixel>& palette);
    void ApplyFloydSteinbergDithering(const std::vector<Pixel>& palette);
    void ApplyOrderedDithering(const std::vector<Pixel>& palette);
    Pixel FindNearestPaletteColor(const Pixel& color, const std::vector<Pixel>& palette) const;
    
    // Undo/Redo system
    std::vector<CanvasSnapshot> undoStack;
    std::vector<CanvasSnapshot> redoStack;
    size_t maxUndoSteps = 50;
    
    void PushUndo(const std::string& description = "");
    void Undo();
    void Redo();
    void ClearUndoStack();
    
    // File I/O
    bool SaveToTGA(const std::string& filename);
    bool SaveToPNG(const std::string& filename);
    bool LoadFromImage(const std::string& filename);
    bool SaveBinary(const std::string& filename);
    bool LoadBinary(const std::string& filename);
    
    // UI State
    ImVec2 canvasPos;
    ImVec2 canvasSize;
    float canvasScale = 1.0f;
    ImVec2 scrollOffset = ImVec2(0, 0);
    
    // Drawing state
    DrawTool currentTool = DrawTool::Pencil;
    Pixel currentColor = Pixel(255, 255, 255, 255);
    BrushSettings brushSettings;
    
    // Tool-specific state
    bool isDrawing = false;
    bool isLineMode = false;
    ImVec2 lineStartPoint;
    ImVec2 lastDrawPoint;
    
    // Palette management
    std::vector<ColorPalette> availablePalettes;
    int selectedPaletteIndex = -1;  // -1 means no palette (free color)
    std::vector<Pixel> customPalette;
    bool paletteEnabled = false;
    bool ditheringEnabled = false;
    
    // Apple Pencil / Pressure sensitivity
    float currentPressure = 1.0f;
    bool pressureSensitivityEnabled = true;
    
    // UI Drawing methods
    void DrawToolbar();
    void DrawColorPicker();
    void DrawPaletteSelector();
    void DrawCanvasView();
    void DrawStatusBar();
    void DrawBrushSettings();
    
    // Input handling
    void HandleCanvasInput();
    void HandleKeyboardShortcuts();
    void ProcessDrawing(const ImVec2& mousePos);
    
    // Utility
    ImVec2 ScreenToCanvas(const ImVec2& screenPos) const;
    ImVec2 CanvasToScreen(const ImVec2& canvasPos) const;
    int GetPixelIndex(int x, int y) const;
    
    // Options
    bool showGrid = false;
    int gridSize = 8;
    bool snapToGrid = false;
    bool showPixelCoords = true;
    bool autoBackup = true;
    
    // File dialog state
    char filenameBuffer[255] = "untitled";
    FileUtils fileUtils;
};