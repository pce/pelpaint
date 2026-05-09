#pragma once

#include "../settings/AppSettings.hpp"
#include <imgui.h>
#include <string>

namespace pelpaint::ui {

// ---------------------------------------------------------------------------
// SettingsDialog — full-screen modal settings UI.
//
// Usage:
//   // Member of PixelPaintView:
//   ui::SettingsDialog settingsDialog_;
//
//   // When user clicks ⚙:
//   settingsDialog_.Open(settings_);
//
//   // Every frame inside Draw():
//   if (settingsDialog_.Draw(settings_))   // returns true on save
//       SDL_Log("Settings saved");
// ---------------------------------------------------------------------------
class SettingsDialog {
public:
    // Request the dialog to open on the next Draw() call.
    // Copies `current` into an internal draft for editing.
    void Open(const AppSettings& current);

    // Call every frame (inside an active ImGui window).
    // Renders the modal when open.  Returns true on save (caller may log/restart).
    // `out` is updated in-place when the user saves.
    bool Draw(AppSettings& out);

    [[nodiscard]] bool IsOpen()        const noexcept { return open_; }
    [[nodiscard]] bool NeedsRestart()  const noexcept { return needsRestart_; }
    void AcknowledgeRestart()                noexcept { needsRestart_ = false; }

private:
    bool        open_         = false;
    bool        pendingOpen_  = false;
    bool        needsRestart_ = false;
    AppSettings draft_;             // working copy being edited
    int         activeSection_  = 0;   // 0 = Keyboard Shortcuts
    int         capturingRow_   = -1;  // row index being captured, -1 = none

    // Section renderers
    void DrawSidebar();
    void DrawKeyboardSection();

    // Per-row helper: returns true when the row's binding changed.
    bool DrawShortcutRow(int rowIdx, const char* label, const char* hint,
                         KeyBinding& binding);

    // Draw a single keyboard-key "chip" badge.
    static void DrawKeyChip(const char* text, ImU32 bgCol, ImU32 borderCol, ImU32 textCol);
    // Convenience: draw a full binding as chip sequence.
    static void DrawBindingChips(const KeyBinding& kb);
};

} // namespace pelpaint::ui
