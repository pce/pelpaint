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
#include <array>
#include <imgui.h>
#include <implot.h>

#include "ColorPalettes.hpp"
#include "core/ImageSurface.hpp"

#if defined(USE_METAL_BACKEND)
    #ifdef __OBJC__
        @protocol MTLDevice;
    #else
        typedef void* id;
    #endif
#endif

namespace pelpaint {

namespace fs = std::filesystem;

struct ImageView {
    const std::uint8_t* data = nullptr; // RGBA8 or another agreed format
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0; // bytes per row
    std::uint32_t channels = 4; // keep flexible, but default RGBA8

    constexpr bool valid() const noexcept {
        return data != nullptr && width > 0 && height > 0 && stride > 0;
    }
};

// Layer data structure - represents a single drawable layer
struct Layer {
    std::string name;                    // Layer name (e.g., "Background", "Foreground")
    std::vector<pelpaint::Pixel> pixelData;        // pelpaint::Pixel buffer for this layer
    float opacity = 1.0f;                // Layer opacity (0.0 - 1.0)
    bool visible = true;                 // Layer visibility toggle
    int zIndex = 0;                      // Z-order (higher = rendered on top)
    bool locked = false;                 // Prevent accidental edits
    ImVec4 blendColor = ImVec4(1,1,1,1); // Tint/blend color
    int blendMode = 0;                   // 0=Normal, 1=Multiply, 2=Screen, 3=Overlay, etc.

    Layer(const std::string& layerName, int width, int height, int z = 0)
        : name(layerName), opacity(1.0f), visible(true), zIndex(z), locked(false)
    {
        pixelData.resize(width * height, pelpaint::Pixel(0, 0, 0, 0)); // Transparent by default
    }

    // Get blended pixel at (x, y)
    pelpaint::Pixel GetPixel(int x, int y, int width, int height) const
    {
        if (x < 0 || x >= width || y < 0 || y >= height) return pelpaint::Pixel(0,0,0,0);
        return pixelData[y * width + x];
    }

    // Set pixel at (x, y)
    void SetPixel(int x, int y, int width, int height, const pelpaint::Pixel& color)
    {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        pixelData[y * width + x] = color;
    }
};

// Undo/Redo snapshot
struct CanvasSnapshot {
    std::vector<Layer> layers;
    int activeLayerIndex;
    int canvasWidth;
    int canvasHeight;
    std::string description;

    CanvasSnapshot(const std::vector<Layer>& l, int activeIdx, int w, int h, const std::string& desc = "")
        : layers(l), activeLayerIndex(activeIdx), canvasWidth(w), canvasHeight(h), description(desc) {}
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
    PolygonSelect,
    BucketFill,
    Clone,
    ShapeRedraw    // Brush: paints shaded shapes continuously while dragging
};

enum class DitheringType {
    Atkinson,
    Stucki,
    FloydSteinberg,
    Ordered
};

enum class GridMode {
    None,
    Lines,
    Dots,
    Crosses
};

// ShapeRedraw tool - shape primitives for the brush tool
enum class ShapeRedrawShape {
    Dot,           // Single pixel
    Circle,        // Filled circle
    Rectangle,     // Filled rectangle
    RectOutline,   // Rectangle outline
    Gem,           // Diamond/gem (two triangles, inverted)
    Diamond        // Diamond shape with shading
};

// ShapeRedraw Filter - shape modes for the Filter tab effect
enum class ShapeRedrawFilterMode {
    Square,        // Pixelify-style: fill block with average/palette color as a square
    Dot,           // Pixelify-style: fill block center with a filled circle
    Custom         // User-defined 8x8 pixel map stamped per block
};

// Background fill mode for Shape Redraw Filter
enum class ShapeRedrawBgMode {
    Black,         // Fill background with black
    White,         // Fill background with white
    Alpha          // Fill background with transparent (alpha=0)
};

// Brush settings
struct BrushSettings {
    float size = 1.0f;
    float pressure = 1.0f;  // For Apple Pencil/pressure-sensitive input
    float opacity = 1.0f;
    bool antialiased = false;
    bool useStepping = false;  // Toggle between slider and input
};

// Selection data structure - supports rectangle, circle, and polygon
struct SelectionData {
    std::vector<pelpaint::Pixel> pixels;
    int width = 0;
    int height = 0;

    // Rectangle selection
    ImVec2 selectionStart;
    ImVec2 selectionEnd;

    // Circle/Polygon selection
    ImVec2 sourceCenter;                              // Center point for circular selection
    std::vector<ImVec2> polygonPoints;                // Vertices for polygon selection

    // Common properties
    bool isActive = false;
    bool canBlur = false;
    float blurAmount = 0.0f;

    // Selection type
    enum SelectionType {
        Rectangle,
        Circle,
        Polygon
    } type = Rectangle;
};



// Right panel tab enumeration
enum class RightPanelTab {
    Tool,           // Tool settings (brush, size, etc)
    Color,          // Color and palette
    Filter,         // Filters: dithering, grayscale, pixelify, shape redraw, etc.
    Layers,         // Layer stack management
    Files           // File I/O
};

class PixelPaintView
{
public:
    PixelPaintView();
    ~PixelPaintView();

    void Draw(std::string_view label);
    void SetupDitheringUI();
    void DiffuseError(int x, int y, int errorR, int errorG, int errorB, int spreadX, int spreadY, int divisor, int totalWeight);
    bool IsValidPixel(int x, int y) const;

    // Persistent ImGui-backed controls:
    // - `AddCheckbox` stores current boolean state in `checkboxValues` keyed by label.
    // Callbacks are invoked when the value changes.
    // Note: Sliders are handled by pelpaint::ui::SliderIntStepStateful / SliderFloatStepStateful in ui/Widgets.hpp
    void AddCheckbox(const std::string& label, const std::function<void(bool)>& callback);
    void AddButton(const std::string& label, const std::function<void()>& callback);

    void ConvertToGrayscale();
    void ApplyAtkinsonDithering(const std::vector<pelpaint::Pixel>& palette);
    void ApplyDithering(DitheringType type, const std::vector<pelpaint::Pixel>& palette);
    void ApplyStuckiDithering(const std::vector<pelpaint::Pixel>& palette);

#if defined(USE_METAL_BACKEND)
    void SetMetalDevice(void* device);
#endif

private:
    // Canvas dimensions
    int canvasWidth = 800;
    int canvasHeight = 600;

    // Core pixel buffer - the "real" image
    std::vector<pelpaint::Pixel> canvasData;
    core::ImageSurface canvasSurface;

    // Layer management system
    std::vector<Layer> layers;                      // Stack of layers (index 0 = bottom)
    int activeLayerIndex = 1;                       // Default active layer (foreground, z-index 2)
    int nextLayerId = 2;                            // Auto-increment for layer names
    bool showLayersPanel = true;                    // Toggle layers panel visibility

    // Layer helper methods
    void InitializeLayers();
    void AddLayer(const std::string& name);
    void RemoveLayer(int layerIndex);
    void ReorderLayers(int fromIndex, int toIndex);
    void CompositeLayers(std::vector<pelpaint::Pixel>& output) const;
    void RenderLayerToCanvas();
    Layer* GetActiveLayer();
    const Layer* GetActiveLayer() const;

    // GPU texture handle (platform-specific)
#if defined(USE_METAL_BACKEND)
    void* metalTexture = nullptr;  // id<MTLTexture> stored as void*
    void* metalDevice = nullptr;   // id<MTLDevice> stored as void*
#else
    unsigned int textureID = 0;
#endif
    bool textureNeedsUpdate = true;

    struct DirtyRect {
        bool dirty = false;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    DirtyRect dirtyRect;
    int textureWidth = 0;
    int textureHeight = 0;

    void MarkDirtyRect(int x, int y, int w, int h);
    void MarkDirtyFull();
    void ClearDirtyRect();

    // Texture management
    void InitializeTexture();
    void UpdateTexture();
    void DestroyTexture();
    void SyncSurfaceFromCanvas();

    // Canvas operations
    void ResizeCanvas(int newWidth, int newHeight);
    void ClearCanvas(const pelpaint::Pixel& color = pelpaint::Pixel(0, 0, 0, 255));

    // pelpaint::Pixel operations
    void PutPixel(int x, int y, const pelpaint::Pixel& color);
    pelpaint::Pixel GetPixel(int x, int y) const;
    bool IsValidCoord(int x, int y) const;

    // Drawing algorithms
    void DrawLineBresenham(int x0, int y0, int x1, int y1, const pelpaint::Pixel& color, float brushSize = 1.0f);
    void DrawCircle(int cx, int cy, int radius, const pelpaint::Pixel& color, bool filled = true);
    void FloodFill(int x, int y, const pelpaint::Pixel& fillColor);
    void DrawSpray(int x, int y, float radius, const pelpaint::Pixel& color, float density = 0.3f);

    // Palette & Dithering
    bool grayscaleToMono = false;           // Floyd-Steinberg / Ordered grayscale-to-mono
    bool atkinsonGrayscaleToMono = false;   // Atkinson grayscale-to-mono
    bool stuckiGrayscaleToMono = false;     // Stucki grayscale-to-mono
    int atkinsonMatrixDistance = 1;
    int stuckiMatrixDistance = 1;
    int selectedDitheringMethod = 0;        // 0=Floyd-Steinberg, 1=Atkinson, 2=Stucki, 3=Ordered

    void ApplyPalette(const std::vector<pelpaint::Pixel>& palette);
    void ApplyFloydSteinbergDithering(const std::vector<pelpaint::Pixel>& palette);
    void ApplyOrderedDithering(const std::vector<pelpaint::Pixel>& palette);
    pelpaint::Pixel FindNearestPaletteColor(const pelpaint::Pixel& color, const std::vector<pelpaint::Pixel>& palette) const;

    // Pixelify/pelpaint::Pixel Art effect
    void ApplyPixelify(int pixelSize, bool usePalette = true);
    int CalculateAutoPixelSize(int imageWidth, int imageHeight) const;

    // ShapeRedraw brush tool - Intelligent shape-based painting with shading
    void DrawShapeRedrawShape(int x, int y, const pelpaint::Pixel& fgColor, const pelpaint::Pixel& bgColor, ShapeRedrawShape shape, int size);
    pelpaint::Pixel GetShadedColor(const pelpaint::Pixel& baseColor, const pelpaint::Pixel& bgColor, bool darker = false);
    bool IsColorLight(const pelpaint::Pixel& color) const;

    // ShapeRedraw filter - applies a shape-based pixelization effect to the canvas
    // Each image block is sampled for color, then drawn as a Square, Dot, or Custom shape.
    void ApplyShapeRedrawFilter();

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
    bool SaveToSVGPixel(const std::string& filename);
    bool SaveToSVGVector(const std::string& filename);
    bool SaveDepthMap(const std::string& filename);
    bool SaveMesh(const std::string& filename);
    bool LoadFromImage(const std::string& filename);
    bool SaveBinary(const std::string& filename);
    bool LoadBinary(const std::string& filename);
    void CropToSelection();
    bool IsRectSelectionActive() const;
    static void IOSOpenFileCallback(void* context, const char* filepath);

    // UI State
    ImVec2 canvasPos;
    ImVec2 canvasSize;
    float canvasScale = 1.0f;
    float userCanvasScale = 1.0f;
    bool fitCanvas = true;

    // Canvas pan state
    ImVec2 panOffset        = ImVec2(0, 0);
    bool   isPanning        = false;
    bool   spaceHeld        = false;
    bool   pinchActive      = false;
    float  pinchStartDist   = 0.0f;
    float  pinchStartScale  = 1.0f;
    ImVec2 scrollOffset     = ImVec2(0, 0);

    // Set by DrawCanvasView before calling HandleCanvasInput
    // so that HandleCanvasInput knows whether the canvas hit area
    // is hovered/active without re-querying the wrong last item.
    bool   canvasHitHovered = false;
    bool   canvasHitActive  = false;

    // Persistent ImGui control state: values are keyed by the control label
    // Note: slider state is now managed inside pelpaint::ui (Widgets.hpp static maps)
    std::unordered_map<std::string, bool> checkboxValues;

    // Drawing state
    DrawTool currentTool = DrawTool::Pencil;
    pelpaint::Pixel currentColor = pelpaint::Pixel(255, 255, 255, 255);
    BrushSettings brushSettings;

    // Eraser state
    bool eraserUseAlpha = true;
    pelpaint::Pixel eraserColor = pelpaint::Pixel(255, 0, 255, 255); // Hot pink for visibility

    // Tool-specific state
    bool isDrawing = false;
    bool isLineMode = false;
    ImVec2 lineStartPoint;
    ImVec2 lastDrawPoint;

    // Clone/Stamp tool state
    bool cloneSourceSet = false;
    ImVec2 cloneSourcePoint = ImVec2(0, 0);

    // Palette management
    std::vector<pelpaint::ColorPalette> availablePalettes;
    int selectedPaletteIndex = 10; // DB32 is index 10 in GetAllPalettes()
    std::vector<pelpaint::Pixel> customPalette;
    bool paletteEnabled = true;
    bool ditheringPreserveAlpha = true;

    // Bucket fill threshold
    float bucketThreshold = 0.0f;
    bool bucketEraseToAlpha = true;

    // Pixelify settings
    int pixelifySize = 4;
    bool pixelifyUsePalette = true;
    bool autoPixelifyOnLoad = true;
    int autoPixelifyThreshold = 800;
    bool showPixelifyPreview = true;

    // Export settings
    int exportTypeIndex = 0;        // 0=Image, 1=SVG, 2=Depth Map, 3=Mesh
    int imageExportFormat = 0;      // 0=PNG, 1=TGA, 2=JPEG
    int meshExportFormat = 0;       // 0=PLY (placeholder for future formats)
    int depthMapGridSize = 4;
    int meshExportGridSize = 4;
    int meshExportMode = 0;

    // ShapeRedraw brush tool settings
    pelpaint::Pixel shapeRedrawBgColor = pelpaint::Pixel(0, 0, 0, 255);
    ShapeRedrawShape shapeRedrawShape = ShapeRedrawShape::Dot;
    int shapeRedrawSize = 4;
    bool shapeRedrawAutoShade = true;

    // ShapeRedraw filter settings
    ShapeRedrawFilterMode shapeRedrawFilterMode = ShapeRedrawFilterMode::Square;
    ShapeRedrawBgMode shapeRedrawBgMode = ShapeRedrawBgMode::Black;
    int shapeRedrawFilterBlockSize = 8;   // Size of each block/cell in pixels
    int shapeRedrawFilterPadding = 1;     // Gap between shapes in pixels
    bool shapeRedrawFilterUsePalette = true;
    // Custom 8x8 shape map: true = foreground pixel drawn, false = background
    std::array<bool, 64> shapeRedrawCustomMap = {};  // Zero-initialized (all off)

    // Selection data
    SelectionData currentSelection;

    // Color picker panel - most frequently used colors
    std::vector<pelpaint::Pixel> frequentColors;
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
    void DrawSelectionOverlay();

    // Input handling
    void HandleCanvasInput();
    void HandleKeyboardShortcuts();
    void ProcessDrawing(const ImVec2& mousePos);

    // Utility
    ImVec2 ScreenToCanvas(const ImVec2& screenPos) const;
    ImVec2 CanvasToScreen(const ImVec2& canvasPos) const;
    int GetPixelIndex(int x, int y) const;

    // Selection operations
    void CopySelection(const ImVec2& startPoint, const ImVec2& endPoint, bool isCircle = false);
    void PasteSelection(const ImVec2& pastePos);
    void BlurSelection(float radius);
    float ColorDistance(const pelpaint::Pixel& c1, const pelpaint::Pixel& c2) const;
    bool IsPointInSelection(int x, int y) const;
    void ClearSelection();

    // Polygon selection
    void AddPolygonPoint(const ImVec2& point);
    void ClearPolygonSelection();
    void FinalizePolygonSelection();
    bool IsPointInPolygon(const ImVec2& point, const std::vector<ImVec2>& polygon) const;
    void CopyPolygonSelection();

    // Enhanced flood fill with threshold
    void FloodFillWithThreshold(int x, int y, const pelpaint::Pixel& fillColor, float threshold);

    // File chooser directory persistence
    void LoadLastDirectory();
    void SaveLastDirectory(const std::string& directory);
    std::string GetHomeDirectory() const;

    // Options
    GridMode gridMode = GridMode::None;
    int gridSize = 8;
    bool snapToGrid = false;
    bool showPixelCoords = true;
    bool autoBackup = true;

    // Grid Helpers
    bool showGridCenter = false;
    bool showGridGoldenRatio = false;

    // UI Panel state
    bool leftToolbarCollapsed = false;
    bool rightPanelCollapsed = false;
    float rightPanelWidth = 380.0f;

    // Right panel tab state
    RightPanelTab currentRightPanelTab = RightPanelTab::Tool;

    // File dialog state
    std::string currentFilename;
    std::string lastDirectory;

    // Helper methods for filename management
    std::string GetDefaultFilename(const std::string& extension = "png");
    void SetFilenameFromLoadedImage(const std::string& imagePath);

    // Right panel drawing methods - organized by tabs
    void DrawRightPanel();
    void DrawToolTab();
    void DrawColorTab();
    void DrawFilterTab();
    void DrawFilesTab();
    void DrawLayersTab();
};

} // namespace pelpaint
