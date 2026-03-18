#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <array>

#include <imgui.h>
#include <implot.h>

#include "core/Types.hpp"
#include "core/Canvas.hpp"
#include "core/UndoHistory.hpp"
#include "ColorPalettes.hpp"
#include "export/ImageExporter.hpp"

#if defined(USE_METAL_BACKEND)
    #ifdef __OBJC__
        @protocol MTLDevice;
    #else
        typedef void* id;
    #endif
#endif

#ifdef __APPLE__
    #include <TargetConditionals.h>
#endif

#include "ImGuiFileDialog.h"

namespace pelpaint {

namespace fs = std::filesystem;


class PixelPaintView {
public:
    PixelPaintView();
    ~PixelPaintView();

    // Entry point called every frame from main loop.
    void Draw(std::string_view label);

    // ---- Metal device (Apple platforms only) ----------------------------
#if defined(USE_METAL_BACKEND)
    void SetMetalDevice(void* device);
#endif

    // ---- iOS file callback (static so it can be passed as C function ptr) --
    static void IOSOpenFileCallback(void* context, const char* filepath);

    // ---- SDL pen / stylus event forwarding ------------------------------
    // Call these from SDL_AppEvent when SDL_EVENT_PEN_AXIS fires so that
    // pressure-sensitive and tilt-aware brush modes receive live hardware data.

    // Update current stylus pressure (0..1). When pressureSensitivityEnabled
    // is true, this also updates brushSettings.pressure.
    void SetCurrentPressure(float pressure) noexcept;

    // Update pen tilt axes (each normalised −1..1).
    // SDL_PEN_AXIS_XTILT → x,  SDL_PEN_AXIS_YTILT → y.
    // Both are stored into brushSettings.tiltX / tiltY for use by
    // BrushMode::Pen (calligraphic nib orientation).
    void SetPenTilt(float x, float y) noexcept;

private:
    // source of truth for pixel data
    Canvas canvas_{ 128, 128 };
    // Convenience aliases kept for the staged migration; delegate to canvas_.
    // TODO Stage-3: replace all canvasWidth / canvasHeight usages in .cpp with
    //               canvas_.Width() / canvas_.Height() and remove these.
    int canvasWidth  = 128;
    int canvasHeight = 128;

    // Sync the cached aliases after any canvas resize / restore.
    void SyncDimsFromCanvas() noexcept {
        canvasWidth  = canvas_.Width();
        canvasHeight = canvas_.Height();
    }

    // ====================================================================
    // Undo / Redo history
    //
    // Replaces: undoStack, redoStack, maxUndoSteps
    // ====================================================================

    UndoHistory<CanvasSnapshot> undo_{ 50 };

    // ====================================================================
    // GPU texture
    // ====================================================================

#if defined(USE_METAL_BACKEND)
    void* metalTexture = nullptr;
    void* metalDevice  = nullptr;
#else
    unsigned int textureID = 0;
#endif
    bool textureNeedsUpdate = false;
    int  textureWidth       = 0;
    int  textureHeight      = 0;

    void InitializeTexture();
    void UpdateTexture();       // reads from canvas_.CompositeSurface()
    void DestroyTexture();

    // ====================================================================
    // Layer management (thin wrappers over canvas_)
    // ====================================================================

    void InitializeLayers();                       // calls canvas_.InitDefaultLayers()
    void AddLayer(const std::string& name);
    void RemoveLayer(int index);
    void ReorderLayers(int from, int to);

    Layer*       GetActiveLayer()       { return canvas_.ActiveLayer(); }
    const Layer* GetActiveLayer() const { return canvas_.ActiveLayer(); }

    // Force a composite + texture refresh (use sparingly — prefer IsDirty flow)
    void RenderLayerToCanvas();

    // CompositeLayers: kept for filter methods that build a temporary flat buffer
    // for export.  Writes into the caller-supplied output vector.
    void CompositeLayers(std::vector<Pixel>& output) const;

    // ====================================================================
    // Undo helpers
    // ====================================================================

    void PushUndo(std::string_view description);
    void Undo();
    void Redo();
    void ClearUndoStack();

    // ====================================================================
    // Canvas operations (delegate to canvas_)
    // ====================================================================

    void ResizeCanvas(int newWidth, int newHeight);
    void ClearCanvas(const Pixel& color = {0, 0, 0, 255});

    // Per-pixel access — canvas_.PutPixel() never composites.
    // Composite happens once per frame via the IsDirty check in Draw().
    void   PutPixel(int x, int y, const Pixel& color);
    Pixel  GetPixel(int x, int y) const;
    bool   IsValidCoord(int x, int y) const noexcept;
    int    GetPixelIndex(int x, int y) const noexcept;

    // ====================================================================
    // Canvas coordinate transforms
    // ====================================================================

    ImVec2 ScreenToCanvas(const ImVec2& screenPos) const;
    ImVec2 CanvasToScreen(const ImVec2& canvasPos)  const;

    // ====================================================================
    // Canvas view / pan / zoom state
    // ====================================================================

    ImVec2 canvasPos  = { 0.f, 0.f };   // screen-space top-left of the canvas
    ImVec2 canvasSize = { 128.f, 128.f };
    float  canvasScale     = 1.0f;      // effective scale (fit * user)
    float  userCanvasScale = 1.0f;      // user zoom multiplier
    bool   fitCanvas       = true;      // fit-to-area mode

    ImVec2 panOffset   = { 0.f, 0.f };
    bool   isPanning   = false;
    bool   spaceHeld   = false;
    bool   pinchActive = false;
    float  pinchStartDist  = 0.f;
    float  pinchStartScale = 1.f;
    ImVec2 scrollOffset    = { 0.f, 0.f };

    // Hit-test results stored by DrawCanvasView, consumed by HandleCanvasInput
    bool canvasHitHovered = false;
    bool canvasHitActive  = false;

    // ====================================================================
    // Drawing tool state
    // ====================================================================

    std::unordered_map<std::string, bool> checkboxValues;

    DrawTool     currentTool  = DrawTool::Pencil;
    Pixel        currentColor = { 0, 0, 0, 255 };
    BrushSettings brushSettings;

    bool  eraserUseAlpha = false;
    Pixel eraserColor    = { 0, 0, 0, 0 };

    bool   isDrawing     = false;
    bool   isLineMode    = false;
    ImVec2 lineStartPoint = { 0.f, 0.f };
    ImVec2 lastDrawPoint  = { 0.f, 0.f };

    bool   cloneSourceSet   = false;
    ImVec2 cloneSourcePoint = { 0.f, 0.f };

    // ====================================================================
    // Colour palette state
    // ====================================================================

    std::vector<ColorPalette> availablePalettes;
    int                       selectedPaletteIndex = 10;    // default: DB32
    std::vector<Pixel>        customPalette;
    bool                      paletteEnabled        = true;
    bool                      ditheringPreserveAlpha = false;

    // ====================================================================
    // Bucket fill
    // ====================================================================

    float bucketThreshold   = 0.0f;
    bool  bucketEraseToAlpha = false;

    // ====================================================================
    // Pixelify
    // ====================================================================

    int  pixelifySize           = 4;
    bool pixelifyUsePalette      = false;
    bool autoPixelifyOnLoad      = false;
    int  autoPixelifyThreshold   = 8;
    bool showPixelifyPreview     = false;

    // ====================================================================
    // Export settings
    // ====================================================================

    int exportTypeIndex    = 0;
    int imageExportFormat  = 0;
    int meshExportFormat   = 0;
    int depthMapGridSize   = 8;
    int meshExportGridSize = 8;
    int meshExportMode     = 0;

    // ====================================================================
    // ShapeRedraw brush
    // ====================================================================

    Pixel              shapeRedrawBgColor       = { 0, 0, 0, 255 };
    ShapeRedrawShape   shapeRedrawShape          = ShapeRedrawShape::Circle;
    int                shapeRedrawSize           = 8;
    bool               shapeRedrawAutoShade      = false;

    ShapeRedrawFilterMode shapeRedrawFilterMode      = ShapeRedrawFilterMode::Square;
    ShapeRedrawBgMode     shapeRedrawBgMode           = ShapeRedrawBgMode::Black;
    int                   shapeRedrawFilterBlockSize  = 8;
    int                   shapeRedrawFilterPadding    = 1;
    bool                  shapeRedrawFilterUsePalette = false;

    std::array<bool, 64>  shapeRedrawCustomMap = {};

    // ====================================================================
    // Selection
    // ====================================================================

    SelectionData currentSelection;

    bool   IsRectSelectionActive() const;
    bool   IsPointInSelection(int x, int y) const;

    void   CopySelection(const ImVec2& startPoint, const ImVec2& endPoint, bool isCircle = false);
    void   PasteSelection(const ImVec2& pastePos);
    void   BlurSelection(float radius);
    void   ClearSelection();
    void   CropToSelection();

    void   AddPolygonPoint(const ImVec2& point);
    void   ClearPolygonSelection();
    void   FinalizePolygonSelection();
    bool   IsPointInPolygon(const Point2f& point, const std::vector<Point2f>& polygon) const;
    void   CopyPolygonSelection();

    // ====================================================================
    // Dithering state
    // ====================================================================

    bool grayscaleToMono         = false;
    bool atkinsonGrayscaleToMono = false;
    bool stuckiGrayscaleToMono   = false;
    int  atkinsonMatrixDistance  = 1;
    int  stuckiMatrixDistance    = 1;
    int  selectedDitheringMethod = 0;

    // ====================================================================
    // Frequent / recent colours
    // ====================================================================

    std::vector<Pixel> frequentColors;
    int                maxMostUsedColors = 16;
    void               UpdateFrequentColors();

    // ====================================================================
    // Pressure sensitivity
    // ====================================================================

    float currentPressure            = 1.0f;
    bool  pressureSensitivityEnabled = false;

    // Live pen tilt values forwarded from SDL_EVENT_PEN_AXIS.
    // BrushMode::Pen uses these to orient the calligraphic nib ellipse.
    float penTiltX = 0.0f;   // SDL_PEN_AXIS_XTILT  (−1..1)
    float penTiltY = 0.0f;   // SDL_PEN_AXIS_YTILT  (−1..1)

    // BrushMode::PixelBrush dwell accumulator.
    // Incremented every frame while the pen is held; reset on mouse release
    // and at the start of each new stroke so spread always begins tight.
    float watercolorAccum_ = 0.0f;

    // ====================================================================
    // Grid / overlay
    // ====================================================================

    GridMode gridMode       = GridMode::None;
    int      gridSize       = 8;
    bool     snapToGrid     = false;
    bool     showPixelCoords = false;
    bool     autoBackup     = false;
    bool     showGridCenter     = false;
    bool     showGridGoldenRatio = false;

    // ====================================================================
    // Panel layout
    // ====================================================================

    bool         leftToolbarCollapsed  = false;
    bool         rightPanelCollapsed   = false;
    float        rightPanelWidth       = 280.0f;
    RightPanelTab currentRightPanelTab = RightPanelTab::Tool;

    // ====================================================================
    // File I/O
    // ====================================================================

    std::string currentFilename;
    std::string lastDirectory;

    std::string GetDefaultFilename(std::string_view ext) const;
    void        SetFilenameFromLoadedImage(const std::string& path);
    std::string GetHomeDirectory() const;
    void        LoadLastDirectory();
    void        SaveLastDirectory(const std::string& dir);

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

    // ====================================================================
    // Filters / effects
    // ====================================================================

    void ConvertToGrayscale();
    void ApplyAtkinsonDithering(const std::vector<Pixel>& palette);
    void ApplyStuckiDithering(const std::vector<Pixel>& palette);
    void ApplyFloydSteinbergDithering(const std::vector<Pixel>& palette);
    void ApplyOrderedDithering(const std::vector<Pixel>& palette);
    void ApplyDithering(DitheringType type, const std::vector<Pixel>& palette);
    void ApplyPalette(const std::vector<Pixel>& palette);
    void ApplyPixelify(int pixelSize, bool usePalette = true);
    void ApplyShapeRedrawFilter();

    void SetupDitheringUI();

    // Helpers used by dithering algorithms
    void   DiffuseError(int x, int y, int errR, int errG, int errB,
                        int spreadX, int spreadY, int divisor, int totalWeight);
    Pixel  FindNearestPaletteColor(const Pixel& color, const std::vector<Pixel>& palette) const;
    float  ColorDistance(const Pixel& a, const Pixel& b) const noexcept;

    // Helpers used by ShapeRedraw
    void  DrawShapeRedrawShape(int x, int y, const Pixel& color,
                                const Pixel& bgColor,
                                ShapeRedrawShape shape, int size);
    Pixel GetShadedColor(const Pixel& baseColor, const Pixel& bgColor,
                          bool darker = false);
    bool  IsColorLight(const Pixel& color) const;
    int   CalculateAutoPixelSize(int imageWidth, int imageHeight) const;

    // ====================================================================
    // UI drawing methods
    // ====================================================================

    void DrawToolbar();
    void DrawColorPicker();
    void DrawPaletteSelector();
    void DrawCanvasView();          // renders canvas texture + grid + overlays
    void DrawStatusBar();
    void DrawBrushSettings();
    void DrawSelectionOverlay();

    void DrawRightPanel();
    void DrawToolTab();
    void DrawColorTab();
    void DrawFilterTab();
    void DrawFilesTab();
    void DrawLayersTab();

    // ====================================================================
    // Input handling
    // ====================================================================

    void HandleCanvasInput();
    void HandleKeyboardShortcuts();
    void ProcessDrawing(const ImVec2& mousePos);

    // Widget helpers
    void AddCheckbox(const std::string& label,
                     const std::function<void(bool)>& callback);
    void AddButton(const std::string& label,
                   const std::function<void()>& callback);

    // Internal drawing primitives (thin wrappers over tools::)
    void DrawLineBresenham(int x0, int y0, int x1, int y1,
                            const Pixel& color, float brushSize = 1.0f);
    void DrawCircle(int cx, int cy, int radius,
                     const Pixel& color, bool filled = true);
    void FloodFill(int x, int y, const Pixel& fillColor);
    void DrawSpray(int x, int y, float radius,
                    const Pixel& color, float density = 0.3f);

    // BrushMode::Pen  — pressure + tilt → calligraphic nib ellipse stroke.
    void DrawPenStroke(int x0, int y0, int x1, int y1,
                       const Pixel& color, float brushSize,
                       float pressure, float tiltX, float tiltY);

    // BrushMode::PixelBrush  — watercolor scatter with dwell spread.
    void DrawPixelBrush(int cx, int cy, float radius,
                        const Pixel& color, float pressure, float accumulation);

    // Flood fill variants (thin wrappers over tools::)
    void FloodFillWithThreshold(int x, int y, const Pixel& fillColor, float threshold);
};

} // namespace pelpaint
