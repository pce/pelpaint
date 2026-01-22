#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <imgui.h>
#include <implot.h>

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
    Spray,
    RectangleSelect,
    CircleSelect,
    BucketFill,
    Clone
};

enum class DitheringType {
    Atkinson,
    Stucki,
    FloydSteinberg,
    Ordered
};

// Brush settings
struct BrushSettings {
    float size = 1.0f;
    float pressure = 1.0f;  // For Apple Pencil/pressure-sensitive input
    float opacity = 1.0f;
    bool antialiased = false;
    bool useStepping = false;  // Toggle between slider and input
};

// Selection data structure
struct SelectionData {
    std::vector<Pixel> pixels;
    int width = 0;
    int height = 0;
    ImVec2 selectionStart;
    ImVec2 selectionEnd;
    ImVec2 sourceCenter;  // Center point for circular selection
    bool isActive = false;
    bool canBlur = false;
    float blurAmount = 0.0f;
};

class PixelPaintView
{
public:
    PixelPaintView();
    ~PixelPaintView();

    void Draw(std::string_view label);
    void SetupDitheringUI(); // Declare SetupDitheringUI method
    void DiffuseError(int x, int y, int errorR, int errorG, int errorB, int spreadX, int spreadY, int divisor, int totalWeight);
    bool IsValidPixel(int x, int y) const;

    // Persistent ImGui-backed controls:
    // - `AddSlider` will create a labeled slider and keep its current value in `sliderValues` keyed by label.
    // - `AddCheckbox` will create a labeled checkbox and keep its current state in `checkboxValues` keyed by label.
    // Both functions keep their existing callback signatures for compatibility; callbacks are invoked when the value changes.
    void AddSlider(const std::string& label, int min, int max, int step, const std::function<void(int)>& callback);
    void AddCheckbox(const std::string& label, const std::function<void(bool)>& callback); // Declare AddCheckbox

    void AddButton(const std::string& label, const std::function<void()>& callback); // Declare AddButton
    void ConvertToGrayscale(); // Declare ConvertToGrayscale
    void ApplyAtkinsonDithering(const std::vector<Pixel>& palette); // Declare ApplyAtkinsonDithering
    void ApplyDithering(DitheringType type, const std::vector<Pixel>& palette); // Declare ApplyDithering
    void ApplyStuckiDithering(const std::vector<Pixel>& palette); // Declare ApplyStuckiDithering




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
#if defined(USE_METAL_BACKEND)
    void* metalTexture = nullptr;  // id<MTLTexture> stored as void*
    void* metalDevice = nullptr;   // id<MTLDevice> stored as void*
#else
    unsigned int textureID = 0;
#endif
    bool textureNeedsUpdate = true;

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
    bool grayscaleToMono = false; // Shared parameter for dithering
    int atkinsonMatrixDistance = 1;
    int stuckiMatrixDistance = 1;
    bool atkinsonGrayscaleToMono = false;
    bool stuckiGrayscaleToMono = false;
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
    bool SaveToJPEG(const std::string& filename, int quality = 90);
    bool LoadFromImage(const std::string& filename);
    bool SaveBinary(const std::string& filename);
    bool LoadBinary(const std::string& filename);

    // UI State
    ImVec2 canvasPos;
    ImVec2 canvasSize;
    float canvasScale = 1.0f;
    ImVec2 scrollOffset = ImVec2(0, 0);

    // Persistent ImGui control state: values are keyed by the control label
    // `AddSlider` will store the current integer value in `sliderValues[label]`.
    // `AddCheckbox` will store the current boolean state in `checkboxValues[label]`.
    // These maps allow the widgets to retain state across frames without external state plumbing.
    std::unordered_map<std::string, int> sliderValues;
    std::unordered_map<std::string, bool> checkboxValues;

    // Drawing state
    DrawTool currentTool = DrawTool::Pencil;
    Pixel currentColor = Pixel(255, 255, 255, 255);
    BrushSettings brushSettings;

    // Eraser state
    bool eraserUseAlpha = true;
    Pixel eraserColor = Pixel(255, 0, 255, 255); // Hot pink for visibility

    // Tool-specific state
    bool isDrawing = false;
    bool isLineMode = false;
    ImVec2 lineStartPoint;
    ImVec2 lastDrawPoint;

    // Clone/Stamp tool state (Issue #2)
    bool cloneSourceSet = false;  // Has user set a source point?
    ImVec2 cloneSourcePoint = ImVec2(0, 0);  // The source location to clone from

    // Palette management
    std::vector<ColorPalette> availablePalettes;
    int selectedPaletteIndex = -1;  // -1 means no palette (free color)
    std::vector<Pixel> customPalette;
    bool paletteEnabled = false;
    bool ditheringEnabled = false;
    bool ditheringPreserveAlpha = true;  // Issue #3: Preserve alpha in dithering

    // Bucket fill threshold (Issue #4)
    float bucketThreshold = 0.0f;
    bool bucketEraseToAlpha = true;  // For eraser bucket tool

    // Selection data (Issue #2)
    SelectionData currentSelection;

    // Color picker panel - most frequently used colors
    std::vector<Pixel> frequentColors;
    int maxMostUsedColors = 16;
    void UpdateFrequentColors();

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
    void DrawSelectionOverlay();  // Draw semi-transparent overlay for selections

    // Input handling
    void HandleCanvasInput();
    void HandleKeyboardShortcuts();
    void ProcessDrawing(const ImVec2& mousePos);

    // Utility
    ImVec2 ScreenToCanvas(const ImVec2& screenPos) const;
    ImVec2 CanvasToScreen(const ImVec2& canvasPos) const;
    int GetPixelIndex(int x, int y) const;

    // Selection operations (Issue #2)
    void CopySelection(const ImVec2& startPoint, const ImVec2& endPoint, bool isCircle = false);
    void PasteSelection(const ImVec2& pastePos);
    void BlurSelection(float radius);
    float ColorDistance(const Pixel& c1, const Pixel& c2) const;

    // Enhanced flood fill with threshold (Issue #4)
    void FloodFillWithThreshold(int x, int y, const Pixel& fillColor, float threshold);

    // Options
    bool showGrid = false;
    int gridSize = 8;
    bool snapToGrid = false;
    bool showPixelCoords = true;
    bool autoBackup = true;

    // UI Panel state
    bool rightPanelCollapsed = false;  // Toggle to hide/show right panel for more canvas space


    // File dialog state
    std::string currentFilename;  // Track loaded/saved filename

    // Helper methods for filename management
    std::string GetDefaultFilename(const std::string& extension = "png");
    void SetFilenameFromLoadedImage(const std::string& imagePath);
};
