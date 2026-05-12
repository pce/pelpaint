#pragma once
// FabBar.hpp — Floating Action Bar for ImGui.
//
// Renders a compact pill-shaped horizontal button row in a borderless,
// always-on-top ImGui overlay window.
//
// Theme-aware: destructive buttons derive their red tint from the existing
// ImGuiCol_Button base (no hardcoded colours); accent buttons use
// ImGuiCol_Header as the tint source.  Push/pop counts are RAII-managed by
// the helper so callers can't mis-count.
//
// Usage:
//   static constexpr pelpaint::ui::FabAction kSel[] = {
//       {"Crop",  "Crop canvas to selection"},
//       {"Copy",  "Copy selection"},
//       {"Cut",   "Cut selection", true},       // destructive → red tint
//       {"Filter..", "Apply filter"},
//   };
//   int hit = pelpaint::ui::FabBar("##selfab", screenPos, kSel);
//   if (hit == 0) CropToSelection();

#include <imgui.h>
#include <span>
#include <string_view>

namespace pelpaint::ui {

// FabAction descriptor
struct FabAction {
    std::string_view label;
    std::string_view tooltip;
    bool destructive = false;   ///< Red-tinted (cut, delete)
    bool accent      = false;   ///< Header-colour tint (primary action)
    bool disabled    = false;   ///< Greyed-out (e.g. Paste with empty clipboard)
};

// FabBar — draw and return index of pressed button (-1 if none pressed).
//
// @param id        Unique ImGui window ID string.
// @param screenPos Top-left position of the overlay window in screen coords.
// @param actions   Span of FabAction descriptors.
// @param btnH      Button height in logical px (0 = auto from font size).
// @param bgAlpha   Window background alpha (default 0.88).
[[nodiscard]] inline int FabBar(const char*                id,
                                 ImVec2                     screenPos,
                                 std::span<const FabAction> actions,
                                 float                      btnH    = 0.f,
                                 float                      bgAlpha = 0.88f)
{
    if (actions.empty()) return -1;
    if (btnH <= 0.f) btnH = ImGui::GetTextLineHeight() + 10.f;

    ImGui::SetNextWindowPos(screenPos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(bgAlpha);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(4.f, 4.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(3.f, 3.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);

    // NOTE: ImGuiWindowFlags_NoFocusOnAppearing is intentionally NOT set.
    //
    // When a window has NoFocusOnAppearing, ImGui skips FocusWindow() (and its
    // internal BringWindowToDisplayFront() call) when the window re-activates
    // after being invisible.  This means the FAB window ends up behind the
    // full-screen main window on its second (and every subsequent) appearance,
    // so only the selection overlay dim is visible but no buttons are shown.
    //
    // Without the flag, FocusWindow() fires exactly once per re-appearance,
    // moving the FAB to the front of the draw-order stack.  All keyboard
    // shortcuts in this app are checked globally in HandleKeyboardShortcuts()
    // before any Begin() calls, so the brief nav-focus shift to the FAB has
    // no practical side-effects.
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration      |
        ImGuiWindowFlags_NoNav             |
        ImGuiWindowFlags_AlwaysAutoResize  |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoMove;

    int clicked = -1;

    if (ImGui::Begin(id, nullptr, kFlags)) {
        const ImVec4 btnBase = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        const ImVec4 hdrBase = ImGui::GetStyleColorVec4(ImGuiCol_Header);
        const ImVec4 hdrHov  = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);

        for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
            const auto& a = actions[static_cast<std::size_t>(i)];
            if (i > 0) ImGui::SameLine();

            int nPushed = 0;
            if (a.destructive) {
                // Red tint: boost red channel, dim green/blue relative to theme base
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(btnBase.x * 0.35f + 0.55f,
                           btnBase.y * 0.25f,
                           btnBase.z * 0.25f,
                           btnBase.w));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(0.78f, 0.22f, 0.22f, 0.92f));
                nPushed = 2;
            } else if (a.accent) {
                ImGui::PushStyleColor(ImGuiCol_Button,        hdrBase);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hdrHov);
                nPushed = 2;
            }

            if (a.disabled) ImGui::BeginDisabled();
            if (ImGui::Button(a.label.data(), ImVec2(0.f, btnH)))
                clicked = i;
            if (a.disabled) ImGui::EndDisabled();

            if (nPushed) ImGui::PopStyleColor(nPushed);

            if (!a.tooltip.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", a.tooltip.data());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(4);

    return clicked;
}

} // namespace pelpaint::ui
