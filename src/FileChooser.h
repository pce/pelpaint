#pragma once

#include <string>
#include <functional>
#include <map>

// Callback for file selection: filepath is empty string if cancelled
using FileChooserCallback = std::function<void(const std::string& filepath)>;

// Forward-declare the Emscripten C callback so the friend declaration inside
// FileChooser refers to the C-linkage version (extern "C" and C++ friend
// declarations must agree on linkage).
#ifdef __EMSCRIPTEN__
extern "C" void file_chooser_on_file_selected(const char* filename);
#endif

class FileChooser
{
public:
    static FileChooser& Instance();

    // Open a file for reading.
    // filters: ".png,.jpg,.tga" format
    // startPath: initial directory (ignored on WASM/iOS)
    void OpenFileDialog(
        const std::string& title,
        const std::string& filters,
        const std::string& startPath,
        FileChooserCallback callback
    );

    // Save a file.
    // suggestedFilename: default filename shown in the dialog
    // filters: ".png,.tga" format
    // On WASM: callback receives "/tmp/<suggestedFilename>"; caller MUST call
    //   TriggerWASMDownload(path) after writing data to that MEMFS path.
    void SaveFileDialog(
        const std::string& title,
        const std::string& filters,
        const std::string& suggestedFilename,
        const std::string& startPath,
        FileChooserCallback callback
    );

    // ── WASM download helper ──────────────────────────────────────────────────
    // Reads a file from Emscripten MEMFS and pushes it as a browser download.
    // No-op on all other platforms.
    // Call this after writing image data to the path returned by SaveFileDialog.
    void TriggerWASMDownload(
        const std::string& memfsPath,
        const std::string& downloadName = ""   // defaults to filename part of memfsPath
    );

    // ── Preferences ──────────────────────────────────────────────────────────
    std::string GetLastUsedDirectory() const;
    void        SetLastUsedDirectory(const std::string& path);
    void        SavePreferences();
    void        LoadPreferences();

    // ── ImGuiFileDialog frame pump (Desktop/Win/Linux only) ──────────────────
    // Call once per frame inside ImGui::NewFrame() … ImGui::Render() scope
    // to display any pending ImGuiFileDialog modals.  No-op on macOS/iOS/WASM.
    void RenderPendingDialogs();

private:
    FileChooser();
    ~FileChooser() = default;

    std::string lastUsedDirectory;
    FileChooserCallback g_CurrentCallback;

#ifdef __EMSCRIPTEN__
    void OpenFileDialog_WASM(
        const std::string& title,
        const std::string& filters,
        FileChooserCallback callback
    );

    void SaveFileDialog_WASM(
        const std::string& title,
        const std::string& filters,
        const std::string& suggestedFilename,
        FileChooserCallback callback
    );

    static std::string ExtensionsToMimeTypes(const std::string& extensions);
#else
    void OpenFileDialog_Native(
        const std::string& title,
        const std::string& filters,
        const std::string& startPath,
        FileChooserCallback callback
    );

    void SaveFileDialog_Native(
        const std::string& title,
        const std::string& filters,
        const std::string& suggestedFilename,
        const std::string& startPath,
        FileChooserCallback callback
    );
#endif

    // Global callback access from C extern
    friend void file_chooser_on_file_selected(const char* filename);
    static FileChooser* g_Instance;
};
