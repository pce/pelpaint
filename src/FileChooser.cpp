#include "FileChooser.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

#ifndef __EMSCRIPTEN__
#include "ImGuiFileDialog.h"
#include <filesystem>
namespace fs = std::filesystem;
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IOS || TARGET_OS_TV
#include "IOSFileManager.h"
#endif
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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

std::string FileChooser::GetLastUsedDirectory() const
{
    return lastUsedDirectory;
}

void FileChooser::SetLastUsedDirectory(const std::string& path)
{
    lastUsedDirectory = path;
    SavePreferences();
}

void FileChooser::LoadPreferences()
{
#ifndef __EMSCRIPTEN__
        auto configPath = fs::path(std::getenv("HOME")) / ".pelpaint" / "file_chooser.txt";
        if (fs::exists(configPath)) {
            std::ifstream file(configPath);
            if (file.is_open()) {
                std::string line;
                if (std::getline(file, line) && fs::exists(line)) {
                    lastUsedDirectory = line;
                }
                file.close();
            }
        }
#endif
}

void FileChooser::SavePreferences()
{
#ifndef __EMSCRIPTEN__
        auto configDir = fs::path(std::getenv("HOME")) / ".pelpaint";
        if (!fs::exists(configDir)) {
            fs::create_directories(configDir);
        }
        auto configPath = configDir / "file_chooser.txt";
        std::ofstream file(configPath);
        if (file.is_open()) {
            file << lastUsedDirectory << std::endl;
            file.close();
        }
#endif
}

#ifdef __EMSCRIPTEN__

std::string FileChooser::ExtensionsToMimeTypes(const std::string& extensions)
{
    std::map<std::string, std::string> mimeMap{
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".bmp", "image/bmp"},
        {".webp", "image/webp"},
        {".tga", "image/x-tga"},
    };

    std::string result;
    std::istringstream iss(extensions);
    std::string ext;

    while (std::getline(iss, ext, ',')) {
        ext.erase(0, ext.find_first_not_of(" \t"));
        ext.erase(ext.find_last_not_of(" \t") + 1);

        auto it = mimeMap.find(ext);
        if (it != mimeMap.end()) {
            if (!result.empty()) result += ",";
            result += it->second;
        }
    }

    return result;
}

extern "C" {
    void file_chooser_on_file_selected(const char* filename) {
        if (FileChooser::g_Instance && FileChooser::g_Instance->g_CurrentCallback) {
            std::string fname = filename ? filename : "";
            FileChooser::g_Instance->g_CurrentCallback(fname);
        }
    }
}

void FileChooser::OpenFileDialog_WASM(
    const std::string& title,
    const std::string& filters,
    FileChooserCallback callback)
{
    std::string mimeTypes = ExtensionsToMimeTypes(filters);

    EM_ASM({
        var mimeTypes = UTF8ToString($0);

        var input = document.createElement('input');
        input.type = 'file';
        input.accept = mimeTypes;
        input.style.display = 'none';

        input.onchange = function(e) {
            var file = e.target.files[0];
            if (file) {
                var reader = new FileReader();
                reader.onload = function(event) {
                    if (typeof Module.fileChooserData === 'undefined') {
                        Module.fileChooserData = {};
                    }
                    Module.fileChooserData[file.name] = new Uint8Array(event.target.result);
                    _file_chooser_on_file_selected(allocateUTF8(file.name));
                };
                reader.readAsArrayBuffer(file);
            } else {
                _file_chooser_on_file_selected(allocateUTF8(""));
            }
            document.body.removeChild(input);
        };

        document.body.appendChild(input);
        input.click();
    }, mimeTypes.c_str());
}

void FileChooser::SaveFileDialog_WASM(
    const std::string& title,
    const std::string& filters,
    const std::string& suggestedFilename,
    FileChooserCallback callback)
{
    // WASM save is handled differently - just return the suggested filename
    if (callback) {
        callback(suggestedFilename);
    }
}

#else

void FileChooser::OpenFileDialog_Native(
    const std::string& title,
    const std::string& filters,
    const std::string& startPath,
    FileChooserCallback callback)
{
#if TARGET_OS_IOS || TARGET_OS_TV
    iOS_OpenFilePicker([](const char* filepath) {
        if (g_Instance && g_Instance->g_CurrentCallback && filepath) {
            g_Instance->g_CurrentCallback(filepath);
        }
    });
#else
    std::string initialPath = !startPath.empty() ? startPath : lastUsedDirectory;
    if (!fs::exists(initialPath)) {
        initialPath = ".";
    }

    ImGuiFileDialog::Instance()->OpenDialog(
        "FileChooserLoadDialog",
        title.c_str(),
        filters.c_str(),
        initialPath.c_str(),
        1,
        nullptr,
        ImGuiFileDialogFlags_Modal
    );

    g_CurrentCallback = callback;
#endif
}

void FileChooser::SaveFileDialog_Native(
    const std::string& title,
    const std::string& filters,
    const std::string& suggestedFilename,
    const std::string& startPath,
    FileChooserCallback callback)
{
#if TARGET_OS_IOS || TARGET_OS_TV
    // iOS save - return suggested path in Documents
    char* docPath = iOS_GetDocumentsPath();
    if (docPath) {
        std::string fullPath = std::string(docPath) + "/" + suggestedFilename;
        free(docPath);
        if (callback) callback(fullPath);
    }
#else
    std::string initialPath = !startPath.empty() ? startPath : lastUsedDirectory;
    if (!fs::exists(initialPath)) {
        initialPath = ".";
    }

    ImGuiFileDialog::Instance()->OpenDialog(
        "FileChooserSaveDialog",
        title.c_str(),
        filters.c_str(),
        initialPath.c_str(),
        suggestedFilename.c_str(),
        1,
        nullptr,
        ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite
    );

    g_CurrentCallback = callback;
#endif
}

#endif

void FileChooser::OpenFileDialog(
    const std::string& title,
    const std::string& filters,
    const std::string& startPath,
    FileChooserCallback callback)
{
#ifdef __EMSCRIPTEN__
    OpenFileDialog_WASM(title, filters, callback);
#else
    OpenFileDialog_Native(title, filters, startPath, callback);
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
    SaveFileDialog_WASM(title, filters, suggestedFilename, callback);
#else
    SaveFileDialog_Native(title, filters, suggestedFilename, startPath, callback);
#endif
}
