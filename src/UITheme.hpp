#ifndef UI_THEME_HPP
#define UI_THEME_HPP

#include "ProjectSettings.hpp"
#include <imgui.h>

/**
 * @namespace UITheme
 * A functional namespace for UI theming.
 * This improves cohesion by treating themes as a stateless transformation
 * of the global ImGuiStyle and providing read-only visual metadata.
 */
namespace UITheme {

    struct VisualMetadata {
        ImVec4 waveform_bg;
        ImVec4 waveform_line;
        ImVec4 waveform_selection;
        ImVec4 grid_bg;
        ImVec4 grid_active;
        ImVec4 grid_beat;
    };

    /**
     * @brief Transforms the global ImGui context to the specified theme.
     */
    void Apply(ThemeType theme);

    /**
     * @brief Retrieves visual parameters for custom drawing (Waveforms, Grids).
     */
    VisualMetadata GetMetadata(ThemeType theme);

    /**
     * @brief Utility to determine if the theme requires dark or light text/overlays.
     */
    bool IsLight(ThemeType theme);

} // namespace UITheme

#endif // UI_THEME_HPP
