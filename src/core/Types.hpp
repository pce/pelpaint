#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../ColorPalettes.hpp"

namespace pelpaint {


struct Point2f {
    float x = 0.0f;
    float y = 0.0f;

    [[nodiscard]] constexpr bool operator==(const Point2f&) const noexcept = default;
};

struct Point2i {
    int x = 0;
    int y = 0;

    [[nodiscard]] constexpr bool operator==(const Point2i&) const noexcept = default;
};

// RGBA float color — replaces ImVec4 inside non-UI structs (e.g. Layer tint)
struct Color4f {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    [[nodiscard]] constexpr bool operator==(const Color4f&) const noexcept = default;
};

// Non-owning image view (RGBA8, borrowed pointer)
struct ImageView {
    const std::uint8_t* data     = nullptr;
    std::uint32_t       width    = 0;
    std::uint32_t       height   = 0;
    std::uint32_t       stride   = 0;    // bytes per row
    std::uint32_t       channels = 4;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return data != nullptr && width > 0 && height > 0 && stride > 0;
    }
};

struct Layer {
    std::string        name;
    std::vector<Pixel> pixelData;    // RGBA pixel buffer (w * h entries)
    float              opacity   = 1.0f;
    bool               visible   = true;
    bool               locked    = false;
    int                zIndex    = 0;
    Color4f            blendColor;   // tint; default opaque white
    int                blendMode = 0; // 0=Normal 1=Multiply 2=Screen 3=Overlay

    Layer() = default;

    Layer(std::string_view layerName, int w, int h, int z = 0)
        : name(layerName)
        , pixelData(static_cast<std::size_t>(w) * static_cast<std::size_t>(h),
                    Pixel{0, 0, 0, 0})
        , zIndex(z)
    {}

    // Convenience accessors (bounds-checked)
    [[nodiscard]] Pixel GetPixel(int x, int y, int w, int h) const noexcept {
        if (x < 0 || x >= w || y < 0 || y >= h) return {};
        const std::size_t i = static_cast<std::size_t>(y) * w + x;
        return i < pixelData.size() ? pixelData[i] : Pixel{};
    }

    void SetPixel(int x, int y, int w, int h, const Pixel& color) noexcept {
        if (x < 0 || x >= w || y < 0 || y >= h) return;
        const std::size_t i = static_cast<std::size_t>(y) * w + x;
        if (i < pixelData.size()) pixelData[i] = color;
    }
};

struct CanvasSnapshot {
    std::vector<Layer> layers;
    int                activeLayerIndex = 0;
    int                canvasWidth      = 0;
    int                canvasHeight     = 0;
    std::string        description;

    CanvasSnapshot() = default;

    CanvasSnapshot(const std::vector<Layer>& l,
                   int                        activeIdx,
                   int                        w,
                   int                        h,
                   std::string_view           desc = "")
        : layers(l)
        , activeLayerIndex(activeIdx)
        , canvasWidth(w)
        , canvasHeight(h)
        , description(desc)
    {}
};

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
    ShapeRedraw,
};

// Filter / effect enumerations

enum class DitheringType {
    Atkinson,
    Stucki,
    FloydSteinberg,
    Ordered,
};

enum class GridMode {
    None,
    Lines,
    Dots,
    Crosses,
};

// ShapeRedraw brush — shape drawn per stamp while dragging
enum class ShapeRedrawShape {
    Dot,
    Circle,
    Rectangle,
    RectOutline,
    Gem,
    Diamond,
};

// ShapeRedraw filter — per-block fill mode on the Filter tab
enum class ShapeRedrawFilterMode {
    Square,   // block average as filled square
    Dot,      // block average as filled circle
    Custom,   // user-defined 8×8 stamp
};

// Background fill mode for the ShapeRedraw filter
enum class ShapeRedrawBgMode {
    Black,
    White,
    Alpha,
};


enum class BrushMode {
    PixelPerfect,
    Pen,
    PixelBrush,
};

// ============================================================
// Brush settings
// ============================================================

struct BrushSettings {
    float      size        = 1.0f;
    float      pressure    = 1.0f;   // Apple Pencil / pressure input (0..1)
    float      opacity     = 1.0f;
    bool       antialiased = false;
    bool       useStepping = false;  // toggle between slider and +/- input

    // Active brush algorithm (affects Pencil and Eraser tools).
    BrushMode  mode        = BrushMode::PixelPerfect;

    // Pen tilt axes, normalised −1..1  (updated from SDL_PEN_AXIS_XTILT/YTILT).
    // Used by BrushMode::Pen to orient the calligraphic nib ellipse.
    float      tiltX       = 0.0f;
    float      tiltY       = 0.0f;
};


struct SelectionData {
    // Copied pixel payload (used for copy/paste)
    std::vector<Pixel>    pixels;
    int                   width  = 0;
    int                   height = 0;

    // Rectangle selection corners (canvas pixel coords)
    Point2f               selectionStart;
    Point2f               selectionEnd;

    // Circle / polygon shared centre
    Point2f               sourceCenter;

    // Polygon vertices (canvas pixel coords)
    std::vector<Point2f>  polygonPoints;

    // State flags
    bool  isActive   = false;
    bool  canBlur    = false;
    float blurAmount = 0.0f;

    enum class Type { Rectangle, Circle, Polygon } type = Type::Rectangle;
};

// Right-panel tab selector
enum class RightPanelTab {
    Tool,
    Color,
    Filter,
    Layers,
    Files,
};


#ifdef IMGUI_VERSION
// Only visible when imgui.h has already been included.
[[nodiscard]] inline Point2f ToPoint2f(ImVec2 v) noexcept { return {v.x, v.y}; }
[[nodiscard]] inline ImVec2  ToImVec2 (Point2f p) noexcept { return {p.x, p.y}; }
[[nodiscard]] inline Color4f ToColor4f(ImVec4 v) noexcept { return {v.x, v.y, v.z, v.w}; }
[[nodiscard]] inline ImVec4  ToImVec4 (Color4f c) noexcept { return {c.r, c.g, c.b, c.a}; }
#endif

} // namespace pelpaint
