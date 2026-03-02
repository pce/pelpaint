#pragma once

#include <string>
#include <functional>
#include <map>

// Callback for file selection: filepath is empty string if cancelled
using FileChooserCallback = std::function<void(const std::string& filepath)>;

class FileChooser
{
public:
    static FileChooser& Instance();

    // Open a file for reading
    // filters: ".png,.jpg,.tga" format
    // startPath: initial directory (ignored on WASM)
    void OpenFileDialog(
        const std::string& title,
        const std::string& filters,
        const std::string& startPath,
        FileChooserCallback callback
    );

    // Save a file
    // suggestedFilename: default filename
    // filters: ".png,.tga" format
    void SaveFileDialog(
        const std::string& title,
        const std::string& filters,
        const std::string& suggestedFilename,
        const std::string& startPath,
        FileChooserCallback callback
    );

    // Get the last used directory
    std::string GetLastUsedDirectory() const;

    // Set the last used directory
    void SetLastUsedDirectory(const std::string& path);

    // Save preferences to disk
    void SavePreferences();

    // Load preferences from disk
    void LoadPreferences();

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

    // For global callback access
    friend void file_chooser_on_file_selected(const char* filename);
    static FileChooser* g_Instance;
};
