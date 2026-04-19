#include "FileChooser.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Platform detection
// ─────────────────────────────────────────────────────────────────────────────
#ifdef __APPLE__
#  include <TargetConditionals.h>
#endif

// Use ImGuiFileDialog only on non-Apple, non-WASM desktop platforms
// (Windows, Linux).  macOS uses NSOpenPanel; iOS uses UIDocumentPickerViewController.
#if !defined(__EMSCRIPTEN__) && !(defined(__APPLE__) && TARGET_OS_IOS) && \
    !(defined(__APPLE__) && TARGET_OS_TV) && !defined(__APPLE__)
#  define USE_IMGUI_FILE_DIALOG 1
#endif

#ifdef USE_IMGUI_FILE_DIALOG
#  include "ImGuiFileDialog.h"
#  include "imgui.h"
#endif

#ifndef __EMSCRIPTEN__
#  include <filesystem>
namespace fs = std::filesystem;
#endif

// iOS file manager (compiled-in only when TARGET_OS_IOS via CMake)
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)
#  include "IOSFileManager.h"
#endif

// Forward-declare the macOS native functions implemented in FileChooser_macOS.mm
#if defined(__APPLE__) && !TARGET_OS_IOS && !TARGET_OS_TV
void FileChooser_macOS_OpenFile(
    const std::string& title,
    const std::string& filters,
    const std::string& startPath,
    FileChooserCallback callback);

void FileChooser_macOS_SaveFile(
    const std::string& title,
    const std::string& filters,
    const std::string& suggestedFilename,
    const std::string& startPath,
    FileChooserCallback callback);
#endif

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Singleton boilerplate
// ─────────────────────────────────────────────────────────────────────────────

FileChooser* FileChooser::g_Instance = nullptr;

FileChooser::FileChooser()
{
    g_Instance = this;

#ifdef __EMSCRIPTEN__
    lastUsedDirectory = "/";
#else
    lastUsedDirectory = ".";
    try {
        lastUsedDirectory = fs::current_path().string();
    } catch (...) {
        lastUsedDirectory = ".";
    }
#endif

    LoadPreferences();
}

FileChooser& FileChooser::Instance()
{
    static FileChooser instance;
    return instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Preferences (desktop only)
// ─────────────────────────────────────────────────────────────────────────────

std::string FileChooser::GetLastUsedDirectory() const { return lastUsedDirectory; }

void FileChooser::SetLastUsedDirectory(const std::string& path)
{
    lastUsedDirectory = path;
    SavePreferences();
}

void FileChooser::LoadPreferences()
{
#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__) || \
    (defined(__APPLE__) && !TARGET_OS_IOS && !TARGET_OS_TV)
#  ifndef __EMSCRIPTEN__
    const char* home = std::getenv("HOME");
    if (!home) return;
    auto configPath = fs::path(home) / ".pelpaint" / "file_chooser.txt";
    if (fs::exists(configPath)) {
        std::ifstream f(configPath);
        std::string line;
        if (f.is_open() && std::getline(f, line) && fs::exists(line))
            lastUsedDirectory = line;
    }
#  endif
#endif
}

void FileChooser::SavePreferences()
{
#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__) || \
    (defined(__APPLE__) && !TARGET_OS_IOS && !TARGET_OS_TV)
#  ifndef __EMSCRIPTEN__
    const char* home = std::getenv("HOME");
    if (!home) return;
    auto configDir  = fs::path(home) / ".pelpaint";
    auto configPath = configDir / "file_chooser.txt";
    if (!fs::exists(configDir))
        fs::create_directories(configDir);
    std::ofstream f(configPath);
    if (f.is_open())
        f << lastUsedDirectory << "\n";
#  endif
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// ImGuiFileDialog frame pump (Win/Linux desktop only)
// ─────────────────────────────────────────────────────────────────────────────

void FileChooser::RenderPendingDialogs()
{
#ifdef USE_IMGUI_FILE_DIALOG
    // ── Open dialog ──
    if (ImGuiFileDialog::Instance()->Display("FileChooserLoadDialog")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
            // Update last used directory
            try { lastUsedDirectory = fs::path(path).parent_path().string(); } catch (...) {}
            if (g_CurrentCallback) g_CurrentCallback(path);
        } else {
            if (g_CurrentCallback) g_CurrentCallback("");
        }
        g_CurrentCallback = nullptr;
        ImGuiFileDialog::Instance()->Close();
    }

    // ── Save dialog ──
    if (ImGuiFileDialog::Instance()->Display("FileChooserSaveDialog")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
            try { lastUsedDirectory = fs::path(path).parent_path().string(); } catch (...) {}
            if (g_CurrentCallback) g_CurrentCallback(path);
        } else {
            if (g_CurrentCallback) g_CurrentCallback("");
        }
        g_CurrentCallback = nullptr;
        ImGuiFileDialog::Instance()->Close();
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// WASM implementation
// ─────────────────────────────────────────────────────────────────────────────

#ifdef __EMSCRIPTEN__

std::string FileChooser::ExtensionsToMimeTypes(const std::string& extensions)
{
    static const std::map<std::string, std::string> mimeMap {
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".bmp",  "image/bmp"},
        {".webp", "image/webp"},
        {".tga",  "image/x-tga"},
    };

    std::string result;
    std::istringstream iss(extensions);
    std::string ext;
    while (std::getline(iss, ext, ',')) {
        ext.erase(0, ext.find_first_not_of(" \t"));
        if (!ext.empty() && ext.back() == ' ') ext.pop_back();
        auto it = mimeMap.find(ext);
        if (it != mimeMap.end()) {
            if (!result.empty()) result += ",";
            result += it->second;
        }
    }
    return result;
}

// C callback invoked from JS after the user selects a file
extern "C" {
    void file_chooser_on_file_selected(const char* filename)
    {
        if (FileChooser::g_Instance && FileChooser::g_Instance->g_CurrentCallback) {
            std::string fname = filename ? filename : "";
            FileChooser::g_Instance->g_CurrentCallback(fname);
            FileChooser::g_Instance->g_CurrentCallback = nullptr; // reset
        }
    }
}

void FileChooser::OpenFileDialog_WASM(
    const std::string& /*title*/,
    const std::string& filters,
    FileChooserCallback callback)
{
    g_CurrentCallback = std::move(callback);

    std::string mimeTypes = ExtensionsToMimeTypes(filters);
    EM_ASM({
        var mimeTypes = UTF8ToString($0);

        var input = document.createElement('input');
        input.type    = 'file';
        input.accept  = mimeTypes || '*/*';
        input.style.display = 'none';

        input.onchange = function(e) {
            var file = e.target.files[0];
            document.body.removeChild(input);
            if (!file) {
                _file_chooser_on_file_selected(0);
                return;
            }
            var reader = new FileReader();
            reader.onload = function(ev) {
                // Write file data into MEMFS /tmp so C++ can read it
                var data = new Uint8Array(ev.target.result);
                var path = '/tmp/' + file.name;
                try {
                    FS.writeFile(path, data);
                    var pPath = stringToNewUTF8(path);
                    _file_chooser_on_file_selected(pPath);
                    _free(pPath);
                } catch(err) {
                    console.error('FileChooser WASM write error:', err);
                    _file_chooser_on_file_selected(0);
                }
            };
            reader.onerror = function() { _file_chooser_on_file_selected(0); };
            reader.readAsArrayBuffer(file);
        };

        // cancelled (no change event fires on cancel in most browsers)
        input.addEventListener('cancel', function() {
            document.body.removeChild(input);
            _file_chooser_on_file_selected(0);
        });

        document.body.appendChild(input);
        input.click();
    }, mimeTypes.c_str());
}

void FileChooser::SaveFileDialog_WASM(
    const std::string& /*title*/,
    const std::string& /*filters*/,
    const std::string& suggestedFilename,
    FileChooserCallback callback)
{
    // Return a stable MEMFS path.  Caller writes image data there, then calls
    // TriggerWASMDownload(path) to push it to the browser download manager.
    std::string savePath = "/tmp/" + suggestedFilename;
    if (callback) callback(savePath);
}

void FileChooser::TriggerWASMDownload(
    const std::string& memfsPath,
    const std::string& downloadName)
{
    // Extract filename from path if no displayName given
    std::string dlName = downloadName;
    if (dlName.empty()) {
        auto pos = memfsPath.rfind('/');
        dlName = (pos != std::string::npos) ? memfsPath.substr(pos + 1) : memfsPath;
    }

    EM_ASM({
        var path = UTF8ToString($0);
        var name = UTF8ToString($1);
        try {
            var data = FS.readFile(path);
            var blob = new Blob([data], { type: 'application/octet-stream' });
            var url  = URL.createObjectURL(blob);
            var a    = document.createElement('a');
            a.href   = url;
            a.download = name;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            setTimeout(function() { URL.revokeObjectURL(url); }, 1500);
        } catch(e) {
            console.error('TriggerWASMDownload failed:', e);
        }
    }, memfsPath.c_str(), dlName.c_str());
}

#else  // ── Native (macOS / iOS / Win / Linux) ─────────────────────────────────

// No-op on non-WASM platforms
void FileChooser::TriggerWASMDownload(const std::string&, const std::string&) {}

// ─────────────────────────────────────────────────────────────────────────────
// OpenFileDialog_Native
// ─────────────────────────────────────────────────────────────────────────────

void FileChooser::OpenFileDialog_Native(
    const std::string& title,
    const std::string& filters,
    const std::string& startPath,
    FileChooserCallback callback)
{
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)
    // ── iOS / tvOS: UIDocumentPickerViewController ──────────────────────────
    g_CurrentCallback = std::move(callback);
    iOS_OpenFilePicker([](const char* filepath) {
        if (g_Instance && g_Instance->g_CurrentCallback) {
            g_Instance->g_CurrentCallback(filepath ? filepath : "");
            g_Instance->g_CurrentCallback = nullptr;
        }
    });

#elif defined(__APPLE__) && TARGET_OS_MAC
    // ── macOS: NSOpenPanel (native) ─────────────────────────────────────────
    std::string dir = !startPath.empty() ? startPath : lastUsedDirectory;
    FileChooser_macOS_OpenFile(title, filters, dir, [this, callback](const std::string& path) {
        if (!path.empty()) {
            try { lastUsedDirectory = fs::path(path).parent_path().string(); } catch (...) {}
            SavePreferences();
        }
        if (callback) callback(path);
    });

#else
    // ── Windows / Linux: ImGuiFileDialog ────────────────────────────────────
    std::string initialPath = !startPath.empty() ? startPath : lastUsedDirectory;
    if (!fs::exists(initialPath)) initialPath = ".";

    ImGuiFileDialog::Instance()->OpenDialog(
        "FileChooserLoadDialog",
        title.c_str(),
        filters.c_str(),
        initialPath.c_str(),
        1, nullptr,
        ImGuiFileDialogFlags_Modal
    );
    g_CurrentCallback = std::move(callback);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// SaveFileDialog_Native
// ─────────────────────────────────────────────────────────────────────────────

void FileChooser::SaveFileDialog_Native(
    const std::string& title,
    const std::string& filters,
    const std::string& suggestedFilename,
    const std::string& startPath,
    FileChooserCallback callback)
{
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)
    // ── iOS: write to Documents directory, return path ──────────────────────
    char* docPath = iOS_GetDocumentsPath();
    if (docPath) {
        std::string fullPath = std::string(docPath) + "/" + suggestedFilename;
        free(docPath);
        if (callback) callback(fullPath);
    } else {
        if (callback) callback("");
    }

#elif defined(__APPLE__) && TARGET_OS_MAC
    // ── macOS: NSSavePanel (native) ─────────────────────────────────────────
    std::string dir = !startPath.empty() ? startPath : lastUsedDirectory;
    FileChooser_macOS_SaveFile(title, filters, suggestedFilename, dir,
        [this, callback](const std::string& path) {
            if (!path.empty()) {
                try { lastUsedDirectory = fs::path(path).parent_path().string(); } catch (...) {}
                SavePreferences();
            }
            if (callback) callback(path);
        });

#else
    // ── Windows / Linux: ImGuiFileDialog ────────────────────────────────────
    std::string initialPath = !startPath.empty() ? startPath : lastUsedDirectory;
    if (!fs::exists(initialPath)) initialPath = ".";

    ImGuiFileDialog::Instance()->OpenDialog(
        "FileChooserSaveDialog",
        title.c_str(),
        filters.c_str(),
        initialPath.c_str(),
        suggestedFilename.c_str(),
        1, nullptr,
        ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite
    );
    g_CurrentCallback = std::move(callback);
#endif
}

#endif  // __EMSCRIPTEN__

// ─────────────────────────────────────────────────────────────────────────────
// Public dispatch
// ─────────────────────────────────────────────────────────────────────────────

void FileChooser::OpenFileDialog(
    const std::string& title,
    const std::string& filters,
    const std::string& startPath,
    FileChooserCallback callback)
{
#ifdef __EMSCRIPTEN__
    OpenFileDialog_WASM(title, filters, std::move(callback));
#else
    OpenFileDialog_Native(title, filters, startPath, std::move(callback));
#endif
}

void FileChooser::SaveFileDialog(
    const std::string& title,
    const std::string& filters,
    const std::string& suggestedFilename,
    const std::string& startPath,
    FileChooserCallback callback)
{
#ifdef __EMSCRIPTEN__
    SaveFileDialog_WASM(title, filters, suggestedFilename, std::move(callback));
#else
    SaveFileDialog_Native(title, filters, suggestedFilename, startPath, std::move(callback));
#endif
}
