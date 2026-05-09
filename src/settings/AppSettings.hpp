#pragma once

#include <array>
#include <string>
#include <string_view>
#include <imgui.h>

namespace pelpaint {

// ---------------------------------------------------------------------------
// KeyBinding — one keyboard shortcut (key + optional Ctrl/Shift/Alt).
// Stored as ImGuiKey + three booleans so it is backend- and OS-agnostic.
// ---------------------------------------------------------------------------
struct KeyBinding {
    ImGuiKey key   = ImGuiKey_None;
    bool     ctrl  = false;
    bool     shift = false;
    bool     alt   = false;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return key != ImGuiKey_None; }
    [[nodiscard]] constexpr bool operator==(const KeyBinding&) const noexcept = default;

    // Returns true the frame the binding fires (IsKeyPressed + modifier check).
    // Must be called from inside an ImGui frame.
    [[nodiscard]] bool JustPressed() const noexcept;

    // Human-readable string e.g. "Ctrl+Z" (for UI display).
    [[nodiscard]] std::string Display() const;

    // Serialized lowercase form e.g. "ctrl+z" (written to the INI file).
    [[nodiscard]] std::string Serialize() const;

    // Parse the serialized form. Returns an invalid (None) binding on failure.
    [[nodiscard]] static KeyBinding Deserialize(std::string_view s);
};

// ---------------------------------------------------------------------------
// AppSettings — all persistent application settings.
//
// Load once at startup; write to disk via Save() from the Settings dialog.
// Changes take effect after the application is restarted.
// ---------------------------------------------------------------------------
struct AppSettings {

    // ---- Keyboard shortcuts ------------------------------------------------
    KeyBinding undo        = { ImGuiKey_Z,     true,  false, false };
    KeyBinding redo        = { ImGuiKey_Y,     true,  false, false };
    KeyBinding zoomIn      = { ImGuiKey_Equal, true,  false, false };
    KeyBinding zoomOut     = { ImGuiKey_Minus, true,  false, false };
    KeyBinding resetZoom   = { ImGuiKey_0,     true,  false, false };
    KeyBinding fitToWindow = { ImGuiKey_F,     false, false, false };
    KeyBinding toggleGrid  = { ImGuiKey_G,     false, false, false };
    KeyBinding clearCanvas = { ImGuiKey_None,  false, false, false };

    // ---- Persistence -------------------------------------------------------

    // Load settings from an INI file.  Returns defaults on any error.
    [[nodiscard]] static AppSettings Load(const std::string& path);

    // Write settings to an INI file.  Creates parent directories if needed.
    // Returns false on I/O failure.
    static bool Save(const AppSettings& s, const std::string& path);

    // Factory: settings with all hard-coded defaults.
    [[nodiscard]] static AppSettings Defaults() noexcept;

    // Platform-appropriate path for the settings file:
    //   Windows : %APPDATA%\pelpaint\settings.ini
    //   macOS   : ~/Library/Application Support/pelpaint/settings.ini
    //   Linux   : $XDG_CONFIG_HOME/pelpaint/settings.ini
    //             (fallback: ~/.config/pelpaint/settings.ini)
    //   iOS     : <Documents>/settings.ini
    [[nodiscard]] static std::string DefaultPath();
};

} // namespace pelpaint
