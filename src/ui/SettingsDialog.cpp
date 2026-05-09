#include "SettingsDialog.hpp"

#include <imgui.h>
#include <imgui_internal.h>  // for PushItemFlag / ImGuiItemFlags_Disabled
#include <string>
#include <array>

namespace pelpaint::ui {

// ---------------------------------------------------------------------------
// Visual constants
// ---------------------------------------------------------------------------
static constexpr float kDialogW     = 700.0f;
static constexpr float kDialogH     = 500.0f;
static constexpr float kSidebarW    = 155.0f;
static constexpr float kFooterH     = 48.0f;
static constexpr float kDividerW    = 1.0f;

// Palette
static constexpr ImU32 kColBg           = IM_COL32( 24,  24,  37, 255);
static constexpr ImU32 kColSidebarBg    = IM_COL32( 18,  18,  30, 255);
static constexpr ImU32 kColSidebarActive= IM_COL32( 66,  99, 235, 255);
static constexpr ImU32 kColSidebarHover = IM_COL32( 35,  35,  58, 255);
static constexpr ImU32 kColDivider      = IM_COL32( 45,  45,  68, 255);
static constexpr ImU32 kColFooter       = IM_COL32( 18,  18,  30, 255);
static constexpr ImU32 kColChipBg       = IM_COL32( 50,  50,  72, 255);
static constexpr ImU32 kColChipBorder   = IM_COL32( 90,  90, 130, 255);
static constexpr ImU32 kColChipText     = IM_COL32(210, 210, 235, 255);
static constexpr ImU32 kColCapturing    = IM_COL32(255, 200,  50, 255);
static constexpr ImU32 kColRowHover     = IM_COL32( 35,  35,  58, 220);
static constexpr ImU32 kColHeaderText   = IM_COL32(155, 155, 200, 255);
static constexpr ImU32 kColHint         = IM_COL32(110, 110, 145, 255);

// ---------------------------------------------------------------------------
// Shortcut entries table
// ---------------------------------------------------------------------------
struct ShortcutEntry {
    const char*             label;
    const char*             hint;
    KeyBinding AppSettings::*binding;
};

static constexpr std::array kEntries {
    ShortcutEntry{ "Undo",           "Revert the last paint action",        &AppSettings::undo        },
    ShortcutEntry{ "Redo",           "Re-apply the last undone action",     &AppSettings::redo        },
    ShortcutEntry{ "Zoom In",        "Increase canvas magnification",       &AppSettings::zoomIn      },
    ShortcutEntry{ "Zoom Out",       "Decrease canvas magnification",       &AppSettings::zoomOut     },
    ShortcutEntry{ "Reset Zoom",     "Return to 1:1 pixel zoom",            &AppSettings::resetZoom   },
    ShortcutEntry{ "Fit to Window",  "Scale canvas to fill the view",       &AppSettings::fitToWindow },
    ShortcutEntry{ "Toggle Grid",    "Show or hide the pixel grid",         &AppSettings::toggleGrid  },
    ShortcutEntry{ "Clear Canvas",   "Erase all pixels on the active layer",&AppSettings::clearCanvas },
};

// ---------------------------------------------------------------------------
// SettingsDialog::Open
// ---------------------------------------------------------------------------
void SettingsDialog::Open(const AppSettings& current) {
    draft_       = current;
    open_        = true;
    pendingOpen_ = true;
    capturingRow_= -1;
}

// ---------------------------------------------------------------------------
// SettingsDialog::Draw
// ---------------------------------------------------------------------------
bool SettingsDialog::Draw(AppSettings& out) {
    if (!open_) return false;

    if (pendingOpen_) {
        ImGui::OpenPopup("Settings##ppmodal");
        pendingOpen_ = false;
    }

    bool saved = false;

    // Centre the modal
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(kDialogW, kDialogH), ImGuiCond_Always);

    constexpr ImGuiWindowFlags kModalFlags =
        ImGuiWindowFlags_NoResize     |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoScrollbar  |
        ImGuiWindowFlags_NoScrollWithMouse;

    // Override ImGui's default background
    ImGui::PushStyleColor(ImGuiCol_PopupBg,      ImGui::ColorConvertU32ToFloat4(kColBg));
    ImGui::PushStyleColor(ImGuiCol_BorderShadow,  ImVec4(0, 0, 0, 0.6f));
    ImGui::PushStyleVar  (ImGuiStyleVar_WindowPadding,  ImVec2(0, 0));
    ImGui::PushStyleVar  (ImGuiStyleVar_WindowRounding, 10.0f);

    if (ImGui::BeginPopupModal("Settings##ppmodal", nullptr, kModalFlags)) {
        // ── Title bar ────────────────────────────────────────────────────
        const ImVec2 winPos  = ImGui::GetWindowPos();
        const float  titleH  = 44.0f;
        ImDrawList*  dl      = ImGui::GetWindowDrawList();

        dl->AddRectFilled(winPos,
                          ImVec2(winPos.x + kDialogW, winPos.y + titleH),
                          IM_COL32(15, 15, 26, 255), 10.0f,
                          ImDrawFlags_RoundCornersTop);

        // Title text
        ImGui::SetCursorPos(ImVec2(20.0f, 12.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 220, 255, 255));
        ImGui::TextUnformatted("*  Settings");
        ImGui::PopStyleColor();

        // Close [X] button
        ImGui::SetCursorPos(ImVec2(kDialogW - 36.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(200, 60, 60, 200));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(160, 40, 40, 255));
        ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding, 6.0f);
        if (ImGui::Button("X##closesettings", ImVec2(24.0f, 24.0f))) {
            open_ = false;
            capturingRow_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        // Divider under title
        dl->AddLine(ImVec2(winPos.x, winPos.y + titleH),
                    ImVec2(winPos.x + kDialogW, winPos.y + titleH),
                    kColDivider, 1.0f);

        // ── Body (sidebar + content) ─────────────────────────────────────
        const float bodyY = titleH;
        const float bodyH = kDialogH - titleH - kFooterH;

        // Sidebar background
        dl->AddRectFilled(ImVec2(winPos.x, winPos.y + bodyY),
                          ImVec2(winPos.x + kSidebarW, winPos.y + bodyY + bodyH),
                          kColSidebarBg);

        // Sidebar child
        ImGui::SetCursorPos(ImVec2(0.0f, bodyY));
        ImGui::BeginChild("##settingsSidebar",
                          ImVec2(kSidebarW, bodyH), false,
                          ImGuiWindowFlags_NoScrollbar);
        DrawSidebar();
        ImGui::EndChild();

        // Vertical divider
        dl->AddLine(ImVec2(winPos.x + kSidebarW, winPos.y + bodyY),
                    ImVec2(winPos.x + kSidebarW, winPos.y + bodyY + bodyH),
                    kColDivider, kDividerW);

        // Content child
        ImGui::SetCursorPos(ImVec2(kSidebarW + kDividerW, bodyY));
        ImGui::BeginChild("##settingsContent",
                          ImVec2(kDialogW - kSidebarW - kDividerW, bodyH),
                          false, ImGuiWindowFlags_None);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));

        if (activeSection_ == 0)
            DrawKeyboardSection();

        ImGui::PopStyleVar();
        ImGui::EndChild();

        // ── Footer ───────────────────────────────────────────────────────
        const float footerY = kDialogH - kFooterH;

        dl->AddLine(ImVec2(winPos.x,         winPos.y + footerY),
                    ImVec2(winPos.x + kDialogW, winPos.y + footerY),
                    kColDivider, 1.0f);
        dl->AddRectFilled(ImVec2(winPos.x, winPos.y + footerY),
                          ImVec2(winPos.x + kDialogW, winPos.y + kDialogH),
                          kColFooter, 10.0f, ImDrawFlags_RoundCornersBottom);

        ImGui::SetCursorPos(ImVec2(16.0f, footerY + 10.0f));

        // Reset to Defaults
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(55, 55, 80, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(75, 75,110, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(40, 40, 65, 255));
        if (ImGui::Button("Reset to Defaults", ImVec2(150.0f, 28.0f))) {
            draft_        = AppSettings::Defaults();
            capturingRow_ = -1;
        }
        ImGui::PopStyleColor(3);

        // Cancel + Save on the right
        const float rightX = kDialogW - 16.0f - 90.0f - 8.0f - 90.0f;
        ImGui::SetCursorPos(ImVec2(rightX, footerY + 10.0f));

        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(55, 55, 80, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(75, 75,110, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(40, 40, 65, 255));
        if (ImGui::Button("Cancel##settingscancel", ImVec2(90.0f, 28.0f))) {
            open_         = false;
            capturingRow_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.0f, 8.0f);

        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32( 66,  99, 235, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32( 88, 120, 255, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32( 50,  80, 200, 255));
        if (ImGui::Button("Save##settingssave", ImVec2(90.0f, 28.0f))) {
            if (AppSettings::Save(draft_, AppSettings::DefaultPath())) {
                out           = draft_;
                needsRestart_ = true;
                saved         = true;
            }
            open_         = false;
            capturingRow_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        ImGui::EndPopup();
    } else {
        open_ = false;
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    return saved;
}

// ---------------------------------------------------------------------------
// DrawSidebar
// ---------------------------------------------------------------------------
void SettingsDialog::DrawSidebar() {
    static constexpr std::array kSections = {
        std::pair{ "\xe2\x8c\xa8  Keyboard", 0 },   // ⌨
    };

    ImGui::SetCursorPos(ImVec2(0.0f, 12.0f));

    for (const auto& [label, idx] : kSections) {
        const bool active = (activeSection_ == idx);
        const ImVec2 pos  = ImGui::GetCursorPos();
        const float  itemH = 34.0f;

        // Highlight background
        if (active) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 sp = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(sp,
                              ImVec2(sp.x + kSidebarW, sp.y + itemH),
                              kColSidebarActive, 6.0f);
        }

        ImGui::SetCursorPos(ImVec2(14.0f, pos.y));
        ImGui::PushStyleColor(ImGuiCol_Text,
            active ? IM_COL32(255,255,255,255) : IM_COL32(180,180,210,255));
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            active ? IM_COL32(0,0,0,0) : kColSidebarHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(0,0,0,0));
        ImGui::PushStyleVar  (ImGuiStyleVar_FramePadding, ImVec2(0,0));

        if (ImGui::Button(label, ImVec2(kSidebarW - 14.0f, itemH)))
            activeSection_ = idx;

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        ImGui::SetCursorPos(ImVec2(0.0f, pos.y + itemH + 2.0f));
        ImGui::Dummy(ImVec2(0.0f, 0.0f)); // required: SetCursorPos must be followed by an item before EndChild (ImGui 1.89+)
    }
}

// ---------------------------------------------------------------------------
// DrawKeyChip — draws a single rounded key badge at the current cursor.
// ---------------------------------------------------------------------------
void SettingsDialog::DrawKeyChip(const char* text, ImU32 bgCol, ImU32 borderCol, ImU32 textCol) {
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float  padX = 8.0f, padY = 4.0f, rounding = 4.0f;
    const ImVec2 chipSize(textSize.x + padX * 2.0f, textSize.y + padY * 2.0f);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList*  dl  = ImGui::GetWindowDrawList();

    dl->AddRectFilled(pos, ImVec2(pos.x + chipSize.x, pos.y + chipSize.y), bgCol, rounding);
    dl->AddRect      (pos, ImVec2(pos.x + chipSize.x, pos.y + chipSize.y), borderCol, rounding);
    dl->AddText      (ImVec2(pos.x + padX, pos.y + padY), textCol, text);

    ImGui::Dummy(chipSize);
}

// ---------------------------------------------------------------------------
// DrawBindingChips — draws all chips for a binding, inline.
// ---------------------------------------------------------------------------
void SettingsDialog::DrawBindingChips(const KeyBinding& kb) {
    if (!kb.IsValid()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kColHint);
        ImGui::TextUnformatted("(unbound)");
        ImGui::PopStyleColor();
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
    if (kb.ctrl)  { DrawKeyChip("Ctrl",  kColChipBg, kColChipBorder, kColChipText); ImGui::SameLine(); }
    if (kb.shift) { DrawKeyChip("Shift", kColChipBg, kColChipBorder, kColChipText); ImGui::SameLine(); }
    if (kb.alt)   { DrawKeyChip("Alt",   kColChipBg, kColChipBorder, kColChipText); ImGui::SameLine(); }
    // Key name chip — extract just the key part from Display()
    auto full = kb.Display();
    // Strip modifier prefixes to get just the key name
    auto lastPlus = full.rfind('+');
    std::string keyName = (lastPlus == std::string::npos) ? full : full.substr(lastPlus + 1);
    DrawKeyChip(keyName.c_str(), kColChipBg, kColChipBorder, kColChipText);
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// DrawShortcutRow
// Returns true when the binding was changed.
// ---------------------------------------------------------------------------
bool SettingsDialog::DrawShortcutRow(int rowIdx, const char* label,
                                     const char* hint, KeyBinding& binding) {
    bool changed = false;

    // Row background on hover
    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    const float  rowH   = 34.0f;
    const float  rowW   = ImGui::GetContentRegionAvail().x;

    ImGui::PushID(rowIdx);

    // Invisible button spanning the full row so we can detect hover/click
    ImGui::SetCursorScreenPos(rowMin);
    ImGui::InvisibleButton("##row", ImVec2(rowW, rowH));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered || capturingRow_ == rowIdx)
        dl->AddRectFilled(rowMin, ImVec2(rowMin.x + rowW, rowMin.y + rowH),
                          kColRowHover, 4.0f);

    // Thin separator line under row
    dl->AddLine(ImVec2(rowMin.x,       rowMin.y + rowH - 1.0f),
                ImVec2(rowMin.x + rowW, rowMin.y + rowH - 1.0f),
                kColDivider);

    // Label column  (left)
    ImGui::SetCursorScreenPos(ImVec2(rowMin.x + 4.0f, rowMin.y + 8.0f));
    ImGui::TextUnformatted(label);

    // Hint column (right of label)
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kColHint);
    ImGui::TextUnformatted(hint);
    ImGui::PopStyleColor();

    // Shortcut column (rightmost)
    constexpr float kShortcutColX = 290.0f;  // fixed x offset from content area
    ImGui::SetCursorScreenPos(ImVec2(rowMin.x + kShortcutColX, rowMin.y + 7.0f));

    if (capturingRow_ == rowIdx) {
        // ---- Capture mode ------------------------------------------------
        // Flash the "press key…" text
        const float t  = static_cast<float>(ImGui::GetTime());
        const ImU32 col = (static_cast<int>(t * 2.0f) % 2 == 0)
                         ? kColCapturing : IM_COL32(200, 150, 30, 255);
        dl->AddText(ImGui::GetCursorScreenPos(), col, "Press key combo...");
        ImGui::Dummy(ImVec2(150.0f, 18.0f));

        // Read key presses
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            capturingRow_ = -1;   // cancelled
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete,    false) ||
                   ImGui::IsKeyPressed(ImGuiKey_Backspace,  false)) {
            binding       = {};   // clear
            capturingRow_ = -1;
            changed       = true;
        } else {
            // Scan for any non-modifier key press
            static constexpr ImGuiKey kScanKeys[] = {
                ImGuiKey_A,ImGuiKey_B,ImGuiKey_C,ImGuiKey_D,ImGuiKey_E,
                ImGuiKey_F,ImGuiKey_G,ImGuiKey_H,ImGuiKey_I,ImGuiKey_J,
                ImGuiKey_K,ImGuiKey_L,ImGuiKey_M,ImGuiKey_N,ImGuiKey_O,
                ImGuiKey_P,ImGuiKey_Q,ImGuiKey_R,ImGuiKey_S,ImGuiKey_T,
                ImGuiKey_U,ImGuiKey_V,ImGuiKey_W,ImGuiKey_X,ImGuiKey_Y,
                ImGuiKey_Z,
                ImGuiKey_0,ImGuiKey_1,ImGuiKey_2,ImGuiKey_3,ImGuiKey_4,
                ImGuiKey_5,ImGuiKey_6,ImGuiKey_7,ImGuiKey_8,ImGuiKey_9,
                ImGuiKey_F1,ImGuiKey_F2,ImGuiKey_F3,ImGuiKey_F4,ImGuiKey_F5,
                ImGuiKey_F6,ImGuiKey_F7,ImGuiKey_F8,ImGuiKey_F9,ImGuiKey_F10,
                ImGuiKey_F11,ImGuiKey_F12,
                ImGuiKey_Space,ImGuiKey_Enter,ImGuiKey_Tab,
                ImGuiKey_Equal,ImGuiKey_Minus,ImGuiKey_LeftBracket,
                ImGuiKey_RightBracket,ImGuiKey_Semicolon,ImGuiKey_Apostrophe,
                ImGuiKey_Comma,ImGuiKey_Period,ImGuiKey_Slash,ImGuiKey_Backslash,
                ImGuiKey_GraveAccent,ImGuiKey_UpArrow,ImGuiKey_DownArrow,
                ImGuiKey_LeftArrow,ImGuiKey_RightArrow,
            };
            for (ImGuiKey k : kScanKeys) {
                if (ImGui::IsKeyPressed(k, false)) {
                    binding = { k, io.KeyCtrl, io.KeyShift, io.KeyAlt };
                    capturingRow_ = -1;
                    changed = true;
                    break;
                }
            }
        }
    } else {
        // ---- Display mode ------------------------------------------------
        DrawBindingChips(binding);
        // Clicking the row starts capture
        if (clicked) capturingRow_ = rowIdx;
    }

    ImGui::PopID();
    // Advance cursor past the row
    ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowH));
    return changed;
}

// ---------------------------------------------------------------------------
// DrawKeyboardSection
// ---------------------------------------------------------------------------
void SettingsDialog::DrawKeyboardSection() {
    // Indent the section content away from the sidebar divider.
    ImGui::SetCursorPos(ImVec2(16.0f, 12.0f));

    // Section heading
    ImGui::PushStyleColor(ImGuiCol_Text, kColHeaderText);
    ImGui::TextUnformatted("Keyboard Shortcuts");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Column headers
    ImGui::PushStyleColor(ImGuiCol_Text, kColHint);
    ImGui::Text("%-24s %-30s Shortcut", "Action", "Description");
    ImGui::PopStyleColor();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    const float  cw = ImGui::GetContentRegionAvail().x;
    dl->AddLine(sp, ImVec2(sp.x + cw, sp.y), kColDivider);
    ImGui::Dummy(ImVec2(cw, 2.0f));

    // Shortcut rows
    for (int i = 0; i < static_cast<int>(kEntries.size()); ++i) {
        const auto& e = kEntries[static_cast<std::size_t>(i)];
        DrawShortcutRow(i, e.label, e.hint, draft_.*(e.binding));
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // Restart notice
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 140, 60, 255));
    ImGui::TextUnformatted("\xe2\x93\x98  Changes take effect after restarting pelpaint.");
    ImGui::PopStyleColor();

#if TARGET_OS_IOS || TARGET_OS_TV
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kColHint);
    ImGui::TextWrapped("On iPad/iPhone, shortcuts require a connected hardware keyboard.");
    ImGui::PopStyleColor();
#endif

    // Click-to-edit hint
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kColHint);
    ImGui::TextUnformatted("Click a row to rebind.  Press Esc to cancel.  Delete/Backspace to clear.");
    ImGui::PopStyleColor();
}

} // namespace pelpaint::ui
