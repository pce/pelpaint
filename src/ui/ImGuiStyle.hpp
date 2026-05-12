#pragma once

#include <imgui.h>
#include <SDL3/SDL.h>
#include <filesystem>
#include "IconsFontAwesome5.h"

namespace pelpaint::ui {


/// Base size (logical pixels) shared by the body font and the merged icon font.
static constexpr float kBaseFontSize = 14.0f;

/// Glyph range loaded from fa-solid-900.ttf — covers all FA5 free-solid icons.
static constexpr ImWchar kFA5GlyphRanges[] = { ICON_MIN_FA5, ICON_MAX_FA5, 0 };

/// Returns the full path to a font file stored in the bundled fonts/ directory.
/// Returns an empty string when the file cannot be located.
inline std::string ResolveResourceFontPath(const char* filename)
{
    namespace fs = std::filesystem;

#if defined(__EMSCRIPTEN__)
    // Emscripten preloads resources into the virtual FS at /fonts/
    return std::string("/fonts/") + filename;
#else
    // SDL3: SDL_GetBasePath() returns a cached const char* — do NOT SDL_free() it.
    const char* sdlBase = SDL_GetBasePath();
    if (!sdlBase) return {};
    fs::path baseDir(sdlBase);

#if defined(__APPLE__)
    // macOS app bundle: executable is in .app/Contents/MacOS/
    //                   resources live in   .app/Contents/Resources/
    auto candidate = baseDir.parent_path() / "Resources" / "fonts" / filename;
    if (fs::exists(candidate)) return candidate.string();

    // iOS app bundle (and non-bundle macOS debug builds): resources sit
    // directly next to the executable.
    candidate = baseDir / "fonts" / filename;
    if (fs::exists(candidate)) return candidate.string();
#else
    // Desktop (Linux / Windows): fonts/ directory next to the executable.
    auto candidate = baseDir / "fonts" / filename;
    if (fs::exists(candidate)) return candidate.string();
#endif

    return {};  // not found
#endif
}

/// Load fonts into the ImGui font atlas.
/// Must be called once after ImGui::CreateContext() and before the first frame.
inline void LoadFonts()
{
    ImGuiIO& io = ImGui::GetIO();

    // Base font: ImGui's built-in ProggyClean — guarantees ASCII always works
    io.Fonts->AddFontDefault();

    // Icon font: Font Awesome 5 Free Solid, merged on top of the base font.
    //            Icons are addressed by their Unicode code-points (ICON_FA_* macros)
    const std::string fa5Path = ResolveResourceFontPath(FONT_ICON_FILE_NAME_FA5S);
    if (!fa5Path.empty())
    {
        ImFontConfig cfg;
        cfg.MergeMode        = true;              // blend icons into base font
        cfg.PixelSnapH       = true;              // crisp icon rendering
        cfg.GlyphMinAdvanceX = kBaseFontSize;     // monospace advance for icons
        cfg.GlyphOffset      = ImVec2(0.f, 1.f);  // fine-tune vertical alignment

        io.Fonts->AddFontFromFileTTF(
            fa5Path.c_str(),
            kBaseFontSize,
            &cfg,
            kFA5GlyphRanges
        );
        SDL_Log("[PelPaint] Icon font loaded: %s\n", fa5Path.c_str());
    }
    else
    {
        SDL_Log("[PelPaint] Warning: %s not found — toolbar will use text labels.\n",
                FONT_ICON_FILE_NAME_FA5S);
    }
}

// Unreal Engine inspired dark theme
inline void SetupUnrealTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Colors - Unreal Engine dark theme
    ImVec4* colors = style.Colors;

    // Background colors (dark gray like Unreal)
    colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.25f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame colors
    colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);

    // Title bar
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.10f, 0.10f, 0.51f);

    // Buttons - Unreal Engine accent (light blue-gray)
    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.37f, 0.48f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.47f, 0.65f, 1.00f);

    // Header
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.27f, 0.37f, 0.48f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.47f, 0.65f, 1.00f);

    // Separator
    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.32f, 0.47f, 0.65f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.32f, 0.47f, 0.65f, 1.00f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.37f, 0.45f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.32f, 0.47f, 0.65f, 1.00f);

    // Checkbox, radio button
    colors[ImGuiCol_CheckMark] = ImVec4(0.32f, 0.47f, 0.65f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.27f, 0.37f, 0.48f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.32f, 0.47f, 0.65f, 1.00f);

    // Text colors
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

    // PlotLines and PlotHistogram
    colors[ImGuiCol_PlotLines] = ImVec4(0.32f, 0.47f, 0.65f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.50f, 0.70f, 0.90f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.32f, 0.47f, 0.65f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.50f, 0.70f, 0.90f, 1.00f);

    // Table colors
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.16f, 0.16f, 0.16f, 0.50f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.27f, 0.37f, 0.48f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.27f, 0.37f, 0.48f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);

    // Docking / Nav highlights
    colors[ImGuiCol_DockingPreview] = ImVec4(0.32f, 0.47f, 0.65f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.32f, 0.47f, 0.65f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);

    // Resize grip
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.32f, 0.37f, 0.45f, 1.00f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.32f, 0.47f, 0.65f, 1.00f);

    // Text selected background
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.32f, 0.47f, 0.65f, 0.50f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.32f, 0.47f, 0.65f, 0.90f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);

    // Style parameters
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(7.0f, 6.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing = 22.0f;
    style.ScrollbarSize = 16.0f;
    style.GrabMinSize = 12.0f;

    // Borders and rounding - subtle Unreal style
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    // Rounding - clean and modern like Unreal
    style.WindowRounding = 5.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;

    // Alignment
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);
    style.LogSliderDeadzone = 4.0f;
}

} // namespace pelpaint::ui
