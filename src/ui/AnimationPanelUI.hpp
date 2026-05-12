#pragma once

// ---------------------------------------------------------------------------
// AnimationPanelUI.hpp — Functional UI building blocks for the animation panel.
//
// Design rationale (Immediate Mode paradigm):
//
//   • State is explicit data (POD structs owned by PixelPaintView) — no hidden
//     singleton "widget objects", no shared global state.
//
//   • Rendering is done by free functions / lambdas that accept only the state
//     they need.  Each render helper is a pure-ish transformation:
//         (state&, const context&) → rendered pixels + possible mutations to state
//
//   • Theme colours come exclusively from ImGui::GetStyleColorVec4 so the panel
//     adapts automatically when the theme changes at runtime (push/pop approach,
//     no cached colour values that go stale).
//
//   • Touch targets scale for iPad/iPhone via ButtonH().  Apple HIG recommends
//     a minimum of 44 pt for iPad; 36 pt is acceptable on iPhone.
//
//   • StyleScope provides RAII push/pop for colour overrides, eliminating
//     manual ImGui::PopStyleColor(N) count tracking.
//
// Suggested architectural direction for future refactors:
// ----------------------------------------------------------
//   Instead of one monolithic DrawXxxPanel() function, each logical section of
//   the panel can become a small free function that receives *only* the state
//   slice it reads/writes.  This makes the data-flow explicit, enables easy
//   unit testing of individual sections, and avoids the "hidden dependency web"
//   that makes large immediate-mode UIs hard to reason about:
//
//     RenderTransportRow(AnimationTimeline&, bool& canvasDirty)
//     RenderFPSRow(AnimationTimeline&, int& presetIdx)
//     RenderFrameMgmtRow(AnimationTimeline&, bool& canvasDirty)
//     RenderEffectsRow(PaletteCycleDialogState&, float timelineFPS)
//     RenderFrameStrip(AnimationTimeline&, bool& canvasDirty)
//
//   A further step towards the monadic / value-oriented ideal:
//     Each section returns an *action* (variant / std::function) rather than
//     executing side effects inline.  The caller then dispatches the action,
//     which keeps render functions free of business logic and makes them trivially
//     testable without a GPU context.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>

#ifdef __APPLE__
#  include <TargetConditionals.h>
#endif

#include "../ColorPalettes.hpp"   // pelpaint::Pixel
#include "../core/Canvas.hpp"     // pelpaint::Canvas, pelpaint::Layer

namespace pelpaint::ui::anim {

// ===========================================================================
// Platform helpers
// ===========================================================================

/// Minimum touch-target height in logical pixels.
/// Apple HIG: 44 pt for iPad, 36 pt acceptable on iPhone, 22 pt on desktop.
[[nodiscard]] inline float ButtonH() noexcept
{
#if defined(TARGET_OS_IPAD) && TARGET_OS_IPAD
    return 44.f;
#elif defined(TARGET_OS_IOS) && TARGET_OS_IOS
    return 36.f;
#else
    return 22.f;
#endif
}

// ===========================================================================
// Semantic theme tokens
//
// Always derived from the *current* active ImGuiStyle so they automatically
// track theme changes.  Use these everywhere in the animation panel instead of
// hardcoded IM_COL32 values.
// ===========================================================================
namespace theme {

/// Primary accent colour (e.g. active button).
[[nodiscard]] inline ImVec4 Accent() noexcept
    { return ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive); }

/// Accent on hover.
[[nodiscard]] inline ImVec4 AccentHovered() noexcept
    { return ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered); }

/// A softer version of Accent (70 % alpha) for non-critical accent buttons.
[[nodiscard]] inline ImVec4 AccentFaded() noexcept
{
    ImVec4 c = Accent();
    c.w *= 0.70f;
    return c;
}

/// 32-bit packed accent colour for ImDrawList calls.
[[nodiscard]] inline ImU32 AccentU32() noexcept
    { return ImGui::ColorConvertFloat4ToU32(Accent()); }

/// Error / warning text colour.
[[nodiscard]] inline ImVec4 ErrorText() noexcept
    { return ImVec4(1.f, 0.4f, 0.4f, 1.f); }

} // namespace theme


// ===========================================================================
// StyleScope — RAII push/pop helper
//
// Eliminates manual ImGui::PopStyleColor(N) bookkeeping.  Colours are popped
// in reverse-push order when the scope leaves, matching ImGui's internal stack.
//
//   {
//       StyleScope sc;
//       sc.Push(ImGuiCol_Button,        theme::Accent());
//       sc.Push(ImGuiCol_ButtonHovered, theme::AccentHovered());
//       ImGui::Button("Save");
//   }  // ← auto-pops both colours
// ===========================================================================
class StyleScope final {
public:
    StyleScope()  = default;
    ~StyleScope() { ImGui::PopStyleColor(count_); }

    StyleScope(const StyleScope&)            = delete;
    StyleScope& operator=(const StyleScope&) = delete;

    void Push(ImGuiCol idx, const ImVec4& col) noexcept
    {
        ImGui::PushStyleColor(idx, col);
        ++count_;
    }

private:
    int count_ = 0;
};


// ===========================================================================
// AccentButtonIf
//
// Draws a regular ImGui button but applies the theme's accent colour trio when
// `active` is true.  Returns true when clicked.
// ===========================================================================
[[nodiscard]] inline bool AccentButtonIf(
    const char* label,
    bool        active,
    ImVec2      sz = {0.f, 0.f})
{
    if (active) {
        StyleScope sc;
        sc.Push(ImGuiCol_Button,        theme::Accent());
        sc.Push(ImGuiCol_ButtonHovered, theme::AccentHovered());
        sc.Push(ImGuiCol_ButtonActive,  theme::AccentHovered());
        return ImGui::Button(label, sz);
    }
    return ImGui::Button(label, sz);
}


// ===========================================================================
// PaletteCycleDialogState
//
// All mutable config for the "Palette Cycle…" modal.  Owned by PixelPaintView
// so parameters persist between opens.  Contains no rendering state.
// ===========================================================================
struct PaletteCycleDialogState {
    int         layerIdx       = 0;     ///< Index of the layer to cycle
    int         paletteStart   = 0;     ///< First colour index in cycle group
    int         paletteEnd     = 3;     ///< Last colour index (inclusive)
    float       fps            = 0.f;   ///< Frame delay override (0 = use timeline FPS)
    bool        quantize       = false; ///< Snap layer colours to palette before baking
    float       matchThreshold = 2.f;   ///< RGBA distance tolerance for pixel matching
    std::string lastError;              ///< Non-empty when the most recent bake failed
};


// ===========================================================================
// DrawPaletteCycleDialog
//
// Renders the "Palette Cycle…" modal popup.
//
// USAGE:
//   // When the button is pressed:
//   ImGui::OpenPopup("Palette Cycle##dlg");
//
//   // Every frame (unconditionally, same ImGui window):
//   if (DrawPaletteCycleDialog(dlg, canvas, palette, fps)) {
//       BakePaletteCycle(...);
//   }
//
// Returns true exactly once, on the frame the user clicks "Bake Cycle".
// ===========================================================================
[[nodiscard]] inline bool DrawPaletteCycleDialog(
    PaletteCycleDialogState&             dlg,
    const pelpaint::Canvas&              canvas,
    std::span<const pelpaint::Pixel>     currentPalette,
    float                                timelineFPS)
{
    // Responsive sizing: full width on small screens, constrained on iPad/desktop.
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float  modalW = std::clamp(screen.x * 0.85f, 260.f, 520.f);
    ImGui::SetNextWindowSize(ImVec2(modalW, -1.f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2(screen.x * 0.5f, screen.y * 0.5f),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f));   // pivot = centre

    constexpr auto kFlags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    if (!ImGui::BeginPopupModal("Palette Cycle##dlg", nullptr, kFlags))
        return false;

    const float btnH  = ButtonH();
    const auto& layers = canvas.Layers();
    const int   palSz  = static_cast<int>(currentPalette.size());

    // ---- Layer picker ----------------------------------------------------
    ImGui::TextUnformatted("Cycling Layer");
    ImGui::SetNextItemWidth(-1.f);

    const char* layerPreview =
        (dlg.layerIdx >= 0 && dlg.layerIdx < static_cast<int>(layers.size()))
        ? layers[static_cast<std::size_t>(dlg.layerIdx)].name.c_str()
        : "(none)";

    if (ImGui::BeginCombo("##cylayer", layerPreview)) {
        for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
            const bool sel = (i == dlg.layerIdx);
            if (ImGui::Selectable(
                    layers[static_cast<std::size_t>(i)].name.c_str(), sel))
                dlg.layerIdx = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::Spacing();

    // ---- Palette colour range --------------------------------------------
    ImGui::TextUnformatted("Palette Colour Range");
    if (palSz == 0) {
        ImGui::TextDisabled("(no palette loaded — select one in the Colour tab)");
    } else {
        dlg.paletteStart = std::clamp(dlg.paletteStart, 0, palSz - 1);
        dlg.paletteEnd   = std::clamp(dlg.paletteEnd,   dlg.paletteStart, palSz - 1);

        ImGui::SetNextItemWidth(-1.f);
        ImGui::SliderInt("Start##pcs", &dlg.paletteStart, 0, palSz - 1);
        ImGui::SetNextItemWidth(-1.f);
        ImGui::SliderInt("End##pce",   &dlg.paletteEnd, dlg.paletteStart, palSz - 1);

        // Colour swatch preview strip.
        const int   rangeN = dlg.paletteEnd - dlg.paletteStart + 1;
        const float padX   = ImGui::GetStyle().WindowPadding.x * 2.f;
        const float swSz   = std::clamp(
            (modalW - padX) / static_cast<float>(rangeN), 4.f, 24.f);

        for (int i = dlg.paletteStart; i <= dlg.paletteEnd && i < palSz; ++i) {
            const auto& px = currentPalette[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            ImGui::ColorButton(
                "##sw",
                ImVec4(px.r / 255.f, px.g / 255.f, px.b / 255.f, px.a / 255.f),
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                ImVec2(swSz, swSz));
            ImGui::PopID();
            if (i < dlg.paletteEnd) ImGui::SameLine(0.f, 2.f);
        }
        ImGui::Spacing();
    }

    // ---- Match threshold ------------------------------------------------
    ImGui::TextUnformatted("Match Threshold");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "RGBA distance (0.1 – 20.0).\n"
            "Lower = exact palette match only.\n"
            "Higher = accepts similar hand-painted colours.\n"
            "2.0 is a good starting point.");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::SliderFloat("##thresh", &dlg.matchThreshold, 0.1f, 20.f, "%.2f");
    ImGui::Spacing();

    // ---- Frame delay override -------------------------------------------
    ImGui::TextUnformatted("Frame Delay (seconds)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Per-frame duration.  0 = use the timeline's global FPS.");
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputFloat("##cycdelay", &dlg.fps, 0.f, 0.f, "%.3f"))
        dlg.fps = std::max(0.f, dlg.fps);
    ImGui::SameLine();
    if (dlg.fps <= 0.f)
        ImGui::TextDisabled("(global: %.1f fps)", timelineFPS);
    else
        ImGui::TextDisabled("(= %.1f fps)", 1.f / std::max(dlg.fps, 0.001f));
    ImGui::Spacing();

    // ---- Quantize toggle ------------------------------------------------
    ImGui::Checkbox("Quantize layer to palette before baking", &dlg.quantize);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Snap all layer pixels to the nearest cycle colour first.\n"
            "Recommended when the layer was painted with approximate colours.");
    ImGui::Spacing();

    // ---- Info -----------------------------------------------------------
    {
        const int cycLen = (palSz > 0)
            ? std::max(1, dlg.paletteEnd - dlg.paletteStart + 1) : 0;
        ImGui::TextDisabled("Will append %d frame%s to the timeline.",
            cycLen, cycLen == 1 ? "" : "s");
        ImGui::Spacing();
    }

    // ---- Error ----------------------------------------------------------
    if (!dlg.lastError.empty()) {
        StyleScope errSc;
        errSc.Push(ImGuiCol_Text, theme::ErrorText());
        ImGui::TextWrapped("Error: %s", dlg.lastError.c_str());
        ImGui::Spacing();
    }

    // ---- Bake / Cancel --------------------------------------------------
    bool bakePressed = false;
    {
        const float innerW = modalW
                             - ImGui::GetStyle().WindowPadding.x * 2.f
                             - ImGui::GetStyle().ItemSpacing.x;
        StyleScope bakeSc;
        bakeSc.Push(ImGuiCol_Button,        theme::Accent());
        bakeSc.Push(ImGuiCol_ButtonHovered, theme::AccentHovered());
        if (ImGui::Button("Bake Cycle##bakebtn", ImVec2(innerW * 0.5f, btnH))) {
            dlg.lastError.clear();
            bakePressed = true;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Generate animation frames for each cycle step.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##cycdlg", ImVec2(-1.f, btnH)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
    return bakePressed;
}


// ===========================================================================
// NoiseFxDialogState
// ===========================================================================
struct NoiseFxDialogState {
    int   frameCount   = 16;
    float noiseScale   = 0.05f;
    float timeSpeed    = 0.25f;
    float noiseOpacity = 1.0f;
    Pixel color1       = {  20,  20,  60, 255 };  ///< Gradient dark end
    Pixel color2       = { 120, 200, 255, 255 };  ///< Gradient bright end
    float frameDelay   = 0.f;
    bool  composite    = true;
    bool  smooth       = true;
    int   smoothSteps  = 1;
    std::string lastError;
};

/// Draws the "Noise FX…" modal.  Open with ImGui::OpenPopup("Noise FX##dlg").
/// Returns true exactly once when the user clicks "Bake".
[[nodiscard]] inline bool DrawNoiseFxDialog(
    NoiseFxDialogState& dlg,
    float               timelineFPS)
{
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float  modalW = std::clamp(screen.x * 0.85f, 260.f, 460.f);
    ImGui::SetNextWindowSize(ImVec2(modalW, -1.f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2(screen.x * 0.5f, screen.y * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    constexpr auto kFlags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    if (!ImGui::BeginPopupModal("Noise FX##dlg", nullptr, kFlags))
        return false;

    const float btnH = ButtonH();

    // ---- Frame count
    ImGui::TextUnformatted("Frames");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::SliderInt("##nfx_fc", &dlg.frameCount, 2, 64);

    // ---- Noise parameters
    ImGui::Spacing();
    ImGui::TextUnformatted("Noise Scale");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Spatial frequency — lower = larger blobs, higher = fine grain.");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::SliderFloat("##nfx_sc", &dlg.noiseScale, 0.005f, 0.3f, "%.3f");

    ImGui::TextUnformatted("Time Speed");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Phase advance per frame.  0 = static noise, 1 = fast drift.");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::SliderFloat("##nfx_ts", &dlg.timeSpeed, 0.0f, 2.f, "%.2f");

    ImGui::TextUnformatted("Opacity");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::SliderFloat("##nfx_op", &dlg.noiseOpacity, 0.f, 1.f, "%.2f");

    // ---- Gradient colours
    ImGui::Spacing();
    ImGui::TextUnformatted("Gradient");
    float c1[4] = { dlg.color1.r/255.f, dlg.color1.g/255.f,
                    dlg.color1.b/255.f, dlg.color1.a/255.f };
    float c2[4] = { dlg.color2.r/255.f, dlg.color2.g/255.f,
                    dlg.color2.b/255.f, dlg.color2.a/255.f };
    if (ImGui::ColorEdit4("Dark##nfx_c1", c1,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        dlg.color1 = { uint8_t(c1[0]*255), uint8_t(c1[1]*255),
                       uint8_t(c1[2]*255), uint8_t(c1[3]*255) };
    }
    ImGui::SameLine();
    if (ImGui::ColorEdit4("Bright##nfx_c2", c2,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        dlg.color2 = { uint8_t(c2[0]*255), uint8_t(c2[1]*255),
                       uint8_t(c2[2]*255), uint8_t(c2[3]*255) };
    }

    // ---- Composite toggle
    ImGui::Spacing();
    ImGui::Checkbox("Composite over canvas", &dlg.composite);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Overlay noise on top of the current canvas layers.");

    // ---- Frame delay
    ImGui::Spacing();
    ImGui::TextUnformatted("Frame Delay (s)  — 0 = global FPS");
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputFloat("##nfx_delay", &dlg.frameDelay, 0.f, 0.f, "%.3f"))
        dlg.frameDelay = std::max(0.f, dlg.frameDelay);
    ImGui::SameLine();
    if (dlg.frameDelay <= 0.f)
        ImGui::TextDisabled("(%.1f fps)", timelineFPS);
    else
        ImGui::TextDisabled("(%.1f fps)", 1.f / std::max(dlg.frameDelay, 0.001f));

    // ---- Smooth options
    ImGui::Spacing();
    ImGui::Checkbox("[x] Smooth — lerp between frames", &dlg.smooth);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Insert linearly-interpolated frames between each baked frame\n"
            "to eliminate strobe/flicker between keyframes.");
    if (dlg.smooth) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        ImGui::SliderInt("Steps##nfx_ss", &dlg.smoothSteps, 1, 8);
    }

    // ---- Info line
    ImGui::Spacing();
    {
        const int total = dlg.smooth
            ? dlg.frameCount + (dlg.frameCount - 1) * dlg.smoothSteps
            : dlg.frameCount;
        ImGui::TextDisabled("Will append %d frame%s.", total, total == 1 ? "" : "s");
    }

    // ---- Error
    if (!dlg.lastError.empty()) {
        StyleScope sc; sc.Push(ImGuiCol_Text, theme::ErrorText());
        ImGui::TextWrapped("Error: %s", dlg.lastError.c_str());
    }

    // ---- Bake / Cancel
    ImGui::Spacing();
    bool bake = false;
    {
        const float innerW = modalW - ImGui::GetStyle().WindowPadding.x * 2.f
                                    - ImGui::GetStyle().ItemSpacing.x;
        StyleScope sc;
        sc.Push(ImGuiCol_Button,        theme::Accent());
        sc.Push(ImGuiCol_ButtonHovered, theme::AccentHovered());
        if (ImGui::Button("Bake Noise##nfxbake", ImVec2(innerW * 0.5f, btnH))) {
            dlg.lastError.clear();
            bake = true;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##nfxcancel", ImVec2(-1.f, btnH)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
    return bake;
}


// ===========================================================================
// ParticleFxDialogState
// ===========================================================================
struct ParticleFxDialogState {
    int   frameCount   = 24;
    float simFps       = 24.f;
    float frameDelay   = 0.f;
    bool  composite    = true;
    bool  smooth       = true;
    int   smoothSteps  = 1;
    // particle config fields
    int   maxParticles = 150;
    float emissionRate = 40.f;
    float velMinX = -60.f, velMaxX = 60.f;
    float velMinY = -80.f, velMaxY = -20.f;
    float minLife = 0.5f,  maxLife = 1.5f;
    float minSize = 1.f,   maxSize = 4.f;
    Pixel colorStart = { 255, 220, 50,  255 };
    Pixel colorEnd   = { 255,  80,  0,    0 };
    std::string lastError;
};

/// Draws the "Particle FX…" modal.  Open with ImGui::OpenPopup("Particle FX##dlg").
/// Returns true exactly once when the user clicks "Bake".
[[nodiscard]] inline bool DrawParticleFxDialog(
    ParticleFxDialogState& dlg,
    float                  timelineFPS)
{
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float  modalW = std::clamp(screen.x * 0.85f, 260.f, 500.f);
    ImGui::SetNextWindowSize(ImVec2(modalW, -1.f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2(screen.x * 0.5f, screen.y * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    constexpr auto kFlags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    if (!ImGui::BeginPopupModal("Particle FX##dlg", nullptr, kFlags))
        return false;

    const float btnH = ButtonH();

    // ---- Simulation setup
    ImGui::TextUnformatted("Frames");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::SliderInt("##pfx_fc", &dlg.frameCount, 2, 128);

    ImGui::TextUnformatted("Simulation FPS");
    ImGui::SetNextItemWidth(120.f);
    ImGui::SliderFloat("##pfx_sfps", &dlg.simFps, 1.f, 60.f, "%.0f fps");

    // ---- Particle parameters
    ImGui::Spacing();
    ImGui::TextUnformatted("Particles / Max");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::SliderInt("##pfx_mp", &dlg.maxParticles, 10, 500);

    ImGui::TextUnformatted("Emission Rate  (particles/s)");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::SliderFloat("##pfx_er", &dlg.emissionRate, 1.f, 200.f, "%.0f");

    ImGui::TextUnformatted("Velocity X  [min, max]");
    ImGui::SetNextItemWidth(-1.f);
    float velX[2] = { dlg.velMinX, dlg.velMaxX };
    if (ImGui::DragFloat2("##pfx_vx", velX, 0.5f, -300.f, 300.f, "%.0f")) {
        dlg.velMinX = velX[0]; dlg.velMaxX = velX[1];
    }
    ImGui::TextUnformatted("Velocity Y  [min, max]");
    ImGui::SetNextItemWidth(-1.f);
    float velY[2] = { dlg.velMinY, dlg.velMaxY };
    if (ImGui::DragFloat2("##pfx_vy", velY, 0.5f, -300.f, 300.f, "%.0f")) {
        dlg.velMinY = velY[0]; dlg.velMaxY = velY[1];
    }

    ImGui::TextUnformatted("Life  [min, max]  seconds");
    ImGui::SetNextItemWidth(-1.f);
    float life[2] = { dlg.minLife, dlg.maxLife };
    if (ImGui::DragFloat2("##pfx_life", life, 0.02f, 0.1f, 10.f, "%.2f")) {
        dlg.minLife = life[0]; dlg.maxLife = std::max(life[1], life[0]);
    }

    ImGui::TextUnformatted("Size  [min, max]  pixels");
    ImGui::SetNextItemWidth(-1.f);
    float sz[2] = { dlg.minSize, dlg.maxSize };
    if (ImGui::DragFloat2("##pfx_sz", sz, 0.1f, 1.f, 32.f, "%.1f")) {
        dlg.minSize = sz[0]; dlg.maxSize = std::max(sz[1], sz[0]);
    }

    // ---- Colors
    ImGui::Spacing();
    ImGui::TextUnformatted("Colours");
    float cs[4] = { dlg.colorStart.r/255.f, dlg.colorStart.g/255.f,
                    dlg.colorStart.b/255.f, dlg.colorStart.a/255.f };
    float ce[4] = { dlg.colorEnd.r/255.f, dlg.colorEnd.g/255.f,
                    dlg.colorEnd.b/255.f, dlg.colorEnd.a/255.f };
    if (ImGui::ColorEdit4("Birth##pfx_cs", cs,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
        dlg.colorStart = { uint8_t(cs[0]*255), uint8_t(cs[1]*255),
                           uint8_t(cs[2]*255), uint8_t(cs[3]*255) };
    ImGui::SameLine();
    if (ImGui::ColorEdit4("Death##pfx_ce", ce,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
        dlg.colorEnd = { uint8_t(ce[0]*255), uint8_t(ce[1]*255),
                         uint8_t(ce[2]*255), uint8_t(ce[3]*255) };

    // ---- Composite & delay
    ImGui::Spacing();
    ImGui::Checkbox("Composite over canvas", &dlg.composite);
    ImGui::Spacing();
    ImGui::TextUnformatted("Frame Delay (s)  — 0 = global FPS");
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputFloat("##pfx_delay", &dlg.frameDelay, 0.f, 0.f, "%.3f"))
        dlg.frameDelay = std::max(0.f, dlg.frameDelay);
    ImGui::SameLine();
    if (dlg.frameDelay <= 0.f)
        ImGui::TextDisabled("(%.1f fps)", timelineFPS);
    else
        ImGui::TextDisabled("(%.1f fps)", 1.f / std::max(dlg.frameDelay, 0.001f));

    // ---- Smooth options
    ImGui::Spacing();
    ImGui::Checkbox("[x] Smooth — lerp between frames", &dlg.smooth);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Insert linearly-interpolated frames between each simulation frame\n"
            "to eliminate strobe effects during fast motion.");
    if (dlg.smooth) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        ImGui::SliderInt("Steps##pfx_ss", &dlg.smoothSteps, 1, 8);
    }

    // ---- Info
    ImGui::Spacing();
    {
        const int total = dlg.smooth
            ? dlg.frameCount + (dlg.frameCount - 1) * dlg.smoothSteps
            : dlg.frameCount;
        ImGui::TextDisabled("Will append %d frame%s.", total, total == 1 ? "" : "s");
    }

    // ---- Error
    if (!dlg.lastError.empty()) {
        StyleScope sc; sc.Push(ImGuiCol_Text, theme::ErrorText());
        ImGui::TextWrapped("Error: %s", dlg.lastError.c_str());
    }

    // ---- Bake / Cancel
    ImGui::Spacing();
    bool bake = false;
    {
        const float innerW = modalW - ImGui::GetStyle().WindowPadding.x * 2.f
                                    - ImGui::GetStyle().ItemSpacing.x;
        StyleScope sc;
        sc.Push(ImGuiCol_Button,        theme::Accent());
        sc.Push(ImGuiCol_ButtonHovered, theme::AccentHovered());
        if (ImGui::Button("Bake Particles##pfxbake", ImVec2(innerW * 0.5f, btnH))) {
            dlg.lastError.clear();
            bake = true;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##pfxcancel", ImVec2(-1.f, btnH)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
    return bake;
}


// ===========================================================================
// SmoothDialogState  — global "Smooth All Frames" pass
// ===========================================================================
struct SmoothDialogState {
    int  stepsPerTransition = 1;
    bool wrap               = false;
    std::string lastResult;   ///< Shown after apply (e.g. "Added 12 frames")
    std::string lastError;
};

/// Draws the "Smooth Frames…" modal.  Open with ImGui::OpenPopup("Smooth Frames##dlg").
/// Returns true exactly once when the user clicks "Apply".
[[nodiscard]] inline bool DrawSmoothDialog(SmoothDialogState& dlg)
{
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float  modalW = std::clamp(screen.x * 0.85f, 240.f, 380.f);
    ImGui::SetNextWindowSize(ImVec2(modalW, -1.f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2(screen.x * 0.5f, screen.y * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    constexpr auto kFlags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    if (!ImGui::BeginPopupModal("Smooth Frames##dlg", nullptr, kFlags))
        return false;

    const float btnH = ButtonH();

    ImGui::TextWrapped(
        "Insert lerp frames between every consecutive pair of timeline frames "
        "to eliminate strobe/cut transitions.");
    ImGui::Spacing();

    ImGui::TextUnformatted("Steps per transition");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Number of interpolated frames inserted between each existing pair.\n"
            "1 = one blend frame per cut (2× frame count),\n"
            "3 = three blend frames per cut (4× frame count).");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::SliderInt("##sm_steps", &dlg.stepsPerTransition, 1, 8);

    ImGui::Spacing();
    ImGui::Checkbox("Wrap (also smooth last → first)", &dlg.wrap);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Append lerp frames after the last frame to blend back to the first.\nUse for looping animations.");

    if (!dlg.lastResult.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", dlg.lastResult.c_str());
    }
    if (!dlg.lastError.empty()) {
        StyleScope sc; sc.Push(ImGuiCol_Text, theme::ErrorText());
        ImGui::TextWrapped("Error: %s", dlg.lastError.c_str());
    }

    ImGui::Spacing();
    bool apply = false;
    {
        const float innerW = modalW - ImGui::GetStyle().WindowPadding.x * 2.f
                                    - ImGui::GetStyle().ItemSpacing.x;
        StyleScope sc;
        sc.Push(ImGuiCol_Button,        theme::Accent());
        sc.Push(ImGuiCol_ButtonHovered, theme::AccentHovered());
        if (ImGui::Button("Apply##smbake", ImVec2(innerW * 0.5f, btnH))) {
            dlg.lastError.clear();
            dlg.lastResult.clear();
            apply = true;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close##smcancel", ImVec2(-1.f, btnH)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
    return apply;
}

} // namespace pelpaint::ui::anim
