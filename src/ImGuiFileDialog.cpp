#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ImGuiFileDialog.h"

#include <cfloat>
#include <cstring>
#include <cstdarg>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <sys/stat.h>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <algorithm>
#include <iostream>

#ifdef USE_STD_FILESYSTEM
#include <filesystem>
#include <exception>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _MSC_VER
#define IGFD_DEBUG_BREAK if (IsDebuggerPresent()) __debugbreak()
#else
#define IGFD_DEBUG_BREAK
#endif

#if defined(__WIN32__) || defined(WIN32) || defined(_WIN32) || defined(__WIN64__) || defined(WIN64) || defined(_WIN64) || defined(_MSC_VER)
#define _IGFD_WIN_
#define stat _stat
#define stricmp _stricmp
#include <cctype>
#ifdef USE_STD_FILESYSTEM
#include <windows.h>
#else
#include "dirent/dirent.h"
#endif
#define PATH_SEP '\\'
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__) || defined(__APPLE__) || defined(__EMSCRIPTEN__)
#define _IGFD_UNIX_
#define stricmp strcasecmp
#include <sys/types.h>
#ifndef USE_STD_FILESYSTEM
#include <dirent.h>
#endif
#define PATH_SEP '/'
#endif

#include "imgui.h"
#include "imgui_internal.h"

#ifdef __cplusplus

namespace IGFD {

#ifndef IS_FLOAT_DIFFERENT
#define IS_FLOAT_DIFFERENT(a, b) (fabs((a) - (b)) > FLT_EPSILON)
#endif
#ifndef IS_FLOAT_EQUAL
#define IS_FLOAT_EQUAL(a, b) (fabs((a) - (b)) < FLT_EPSILON)
#endif

namespace Utils {

IGFD_API bool ImSplitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size) {
    using namespace ImGui;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID("##Splitter");
    ImRect bb;
    bb.Min = window->DC.CursorPos + (split_vertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
    bb.Max = bb.Min + CalcItemSize(split_vertically ? ImVec2(thickness, splitter_long_axis_size) : ImVec2(splitter_long_axis_size, thickness), 0.0f, 0.0f);
    return SplitterBehavior(bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 1.0f);
}

IGFD_API std::string UTF8Encode(const std::wstring& wstr) {
    std::string res;
#ifdef _IGFD_WIN_
    if (!wstr.empty()) {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        if (size_needed) {
            res = std::string(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &res[0], size_needed, NULL, NULL);
        }
    }
#else
    (void)wstr;
#endif
    return res;
}

IGFD_API std::wstring UTF8Decode(const std::string& str) {
    std::wstring res;
#ifdef _IGFD_WIN_
    if (!str.empty()) {
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        if (size_needed) {
            res = std::wstring(size_needed, 0);
            MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &res[0], size_needed);
        }
    }
#else
    (void)str;
#endif
    return res;
}

IGFD_API bool ReplaceString(std::string& str, const ::std::string& oldStr, const ::std::string& newStr, const size_t& vMaxRecursion) {
    if (!str.empty() && oldStr != newStr) {
        bool res = false;
        size_t pos = 0;
        bool found = false;
        size_t max_recursion = vMaxRecursion;
        do {
            pos = str.find(oldStr, pos);
            if (pos != std::string::npos) {
                found = res = true;
                str.replace(pos, oldStr.length(), newStr);
                pos += newStr.length();
            } else if (found && max_recursion > 0) {
                found = false;
                pos = 0;
                --max_recursion;
            }
        } while (pos != std::string::npos);
        return res;
    }
    return false;
}

IGFD_API std::vector<std::string> SplitStringToVector(const std::string& vText, const char& vDelimiter, const bool& vPushEmpty) {
    std::vector<std::string> arr;
    if (!vText.empty()) {
        size_t start = 0;
        size_t end = vText.find(vDelimiter, start);
        while (end != std::string::npos) {
            auto token = vText.substr(start, end - start);
            if (!token.empty() || (token.empty() && vPushEmpty)) {
                arr.push_back(token);
            }
            start = end + 1;
            end = vText.find(vDelimiter, start);
        }
        auto token = vText.substr(start);
        if (!token.empty() || (token.empty() && vPushEmpty)) {
            arr.push_back(token);
        }
    }
    return arr;
}

IGFD_API std::vector<std::string> GetDrivesList() {
    std::vector<std::string> res;
#ifdef _IGFD_WIN_
    const DWORD mydrives = 2048;
    char lpBuffer[2048];
    const DWORD countChars = GetLogicalDriveStringsA(mydrives, lpBuffer);
    if (countChars > 0U && countChars < 2049U) {
        std::string var = std::string(lpBuffer, (size_t)countChars);
        ReplaceString(var, "\\", "");
        res = SplitStringToVector(var, '\0', false);
    }
#endif
    return res;
}

IGFD_API bool IsDirectoryCanBeOpened(const std::string& name) {
    bool bExists = false;
    if (!name.empty()) {
#ifdef USE_STD_FILESYSTEM
        namespace fs = std::filesystem;
#ifdef _IGFD_WIN_
        std::wstring wname = UTF8Decode(name.c_str());
        fs::path pathName = fs::path(wname);
#else
        fs::path pathName = fs::path(name);
#endif
        try {
            bExists = fs::is_directory(pathName);
            const auto dir_iter = std::filesystem::directory_iterator(pathName);
            (void)dir_iter;
        } catch (...) {
            bExists = false;
        }
#else
        DIR* pDir = opendir(name.c_str());
        if (pDir != nullptr) {
            bExists = true;
            (void)closedir(pDir);
        }
#endif
    }
    return bExists;
}

IGFD_API bool IsDirectoryExist(const std::string& name) {
    bool bExists = false;
    if (!name.empty()) {
#ifdef USE_STD_FILESYSTEM
        namespace fs = std::filesystem;
#ifdef _IGFD_WIN_
        std::wstring wname = UTF8Decode(name.c_str());
        fs::path pathName = fs::path(wname);
#else
        fs::path pathName = fs::path(name);
#endif
        bExists = fs::is_directory(pathName);
#else
        DIR* pDir = opendir(name.c_str());
        if (pDir) {
            bExists = true;
            closedir(pDir);
        } else if (ENOENT != errno) {
            bExists = true;
        }
#endif
    }
    return bExists;
}

IGFD_API bool CreateDirectoryIfNotExist(const std::string& name) {
    bool res = false;
    if (!name.empty()) {
        if (!IsDirectoryExist(name)) {
#ifdef _IGFD_WIN_
#ifdef USE_STD_FILESYSTEM
            namespace fs = std::filesystem;
            std::wstring wname = UTF8Decode(name.c_str());
            fs::path pathName = fs::path(wname);
            res = fs::create_directory(pathName);
#else
            std::wstring wname = UTF8Decode(name);
            if (CreateDirectoryW(wname.c_str(), nullptr)) res = true;
#endif
#elif defined(__EMSCRIPTEN__)
            std::string str = std::string("FS.mkdir('") + name + "');";
            emscripten_run_script(str.c_str());
            res = true;
#elif defined(_IGFD_UNIX_)
#ifdef USE_STD_FILESYSTEM
            namespace fs = std::filesystem;
            res = fs::create_directory(fs::path(name));
#endif
#endif
        }
    }
    return res;
}

IGFD_API PathStruct ParsePathFileName(const std::string& vPathFileName) {
#ifdef USE_STD_FILESYSTEM
    namespace fs = std::filesystem;
    PathStruct res;
    if (vPathFileName.empty()) return res;
    auto fsPath = fs::path(vPathFileName);
    if (fs::is_directory(fsPath)) {
        res.name = "";
        res.path = fsPath.string();
        res.isOk = true;
    } else {
        res.name = fsPath.filename().string();
        res.path = fsPath.parent_path().string();
        res.isOk = true;
    }
    return res;
#else
    PathStruct res;
    if (!vPathFileName.empty()) {
        std::string pfn = vPathFileName;
        std::string separator(1u, PATH_SEP);
        ReplaceString(pfn, "\\", separator);
        ReplaceString(pfn, "/", separator);
        size_t lastSlash = pfn.find_last_of(separator);
        if (lastSlash != std::string::npos) {
            res.name = pfn.substr(lastSlash + 1);
            res.path = pfn.substr(0, lastSlash);
            res.isOk = true;
        }
        size_t lastPoint = pfn.find_last_of('.');
        if (lastPoint != std::string::npos) {
            if (!res.isOk) { res.name = pfn; res.isOk = true; }
            res.ext = pfn.substr(lastPoint + 1);
            ReplaceString(res.name, "." + res.ext, "");
        }
        if (!res.isOk) { res.name = std::move(pfn); res.isOk = true; }
    }
    return res;
#endif
}

IGFD_API void AppendToBuffer(char* vBuffer, size_t vBufferLen, const std::string& vStr) {
    std::string st = vStr;
    size_t len = vBufferLen - 1u;
    size_t slen = strlen(vBuffer);
    if (!st.empty() && st != "\n") {
        ReplaceString(st, "\n", "");
        ReplaceString(st, "\r", "");
    }
    vBuffer[slen] = '\0';
    std::string str = std::string(vBuffer) + vStr;
    if (len > str.size()) len = str.size();
#ifdef _MSC_VER
    strncpy_s(vBuffer, vBufferLen, str.c_str(), len);
#else
    strncpy(vBuffer, str.c_str(), len);
#endif
    vBuffer[len] = '\0';
}

IGFD_API void ResetBuffer(char* vBuffer) { vBuffer[0] = '\0'; }

IGFD_API void SetBuffer(char* vBuffer, size_t vBufferLen, const std::string& vStr) {
    ResetBuffer(vBuffer);
    AppendToBuffer(vBuffer, vBufferLen, vStr);
}

IGFD_API std::string LowerCaseString(const std::string& vString) {
    auto str = vString;
    for (char& c : str) c = (char)std::tolower(c);
    return str;
}

} // namespace Utils

IGFD_API FileStyle::FileStyle() : color(0, 0, 0, 0) {}
IGFD_API FileStyle::FileStyle(const FileStyle& vStyle) : color(vStyle.color), icon(vStyle.icon), font(vStyle.font), flags(vStyle.flags) {}
IGFD_API FileStyle::FileStyle(const ImVec4& vColor, const std::string& vIcon, ImFont* vFont) : color(vColor), icon(vIcon), font(vFont) {}

IGFD_API void SearchManager::Clear() {
    puSearchTag.clear();
    Utils::ResetBuffer(puSearchBuffer);
}

IGFD_API void SearchManager::DrawSearchBar(FileDialogInternal& vFileDialogInternal) {
    if (ImGui::Button("R##BtnImGuiFileDialogSearchField")) {
        Clear();
        vFileDialogInternal.puFileManager.ApplyFilteringOnFileList(vFileDialogInternal);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset search");
    ImGui::SameLine();
    ImGui::Text("Search :");
    ImGui::SameLine();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    bool edited = ImGui::InputText("##InputImGuiFileDialogSearchField", puSearchBuffer, MAX_FILE_DIALOG_NAME_BUFFER);
    if (ImGui::GetItemID() == ImGui::GetActiveID()) puSearchInputIsActive = true;
    ImGui::PopItemWidth();
    if (edited) {
        puSearchTag = puSearchBuffer;
        vFileDialogInternal.puFileManager.ApplyFilteringOnFileList(vFileDialogInternal);
    }
}

IGFD_API FileDialog::FileDialog() {}
IGFD_API FileDialog::~FileDialog() {}

IGFD_API void FileDialog::OpenDialog(const std::string& vKey, const std::string& vTitle, const char* vFilters, const std::string& vPath, const int& vCountSelectionMax, void* vUserDatas, int vFlags) {
    m_FileDialogInternal.puKey = vKey;
    m_FileDialogInternal.puTitle = vTitle;
    m_FileDialogInternal.puFilters = vFilters;
    m_FileDialogInternal.puFileManager.puCurrentPath = vPath;
    m_FileDialogInternal.puCountSelectionMax = vCountSelectionMax;
    m_FileDialogInternal.puUserDatas = vUserDatas;
    m_FileDialogInternal.puFlags = vFlags;
    m_FileDialogInternal.puIsOpened = true;
}

IGFD_API bool FileDialog::Display(const std::string& vKey, int vFlags, ImVec2 vMinSize, ImVec2 vMaxSize) {
    if (m_FileDialogInternal.puIsOpened && m_FileDialogInternal.puKey == vKey) {
        ImGui::SetNextWindowSizeConstraints(vMinSize, vMaxSize);
        if (ImGui::Begin(m_FileDialogInternal.puTitle.c_str(), &m_FileDialogInternal.puIsOpened, vFlags)) {
            // Very simplified display logic for brevity, normally calls internal draw methods
            if (ImGui::Button("OK")) { m_FileDialogInternal.puIsOk = true; m_FileDialogInternal.puIsOpened = false; }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { m_FileDialogInternal.puIsOk = false; m_FileDialogInternal.puIsOpened = false; }
        }
        ImGui::End();
        return true;
    }
    return false;
}

IGFD_API bool FileDialog::IsOk() const { return m_FileDialogInternal.puIsOk; }
IGFD_API void FileDialog::Close() { m_FileDialogInternal.puIsOpened = false; m_FileDialogInternal.puIsOk = false; }
IGFD_API std::string FileDialog::GetFilePathName(int) { return m_FileDialogInternal.puFileManager.puCurrentPath; }

IGFD_API void FileManager::ApplyFilteringOnFileList(FileDialogInternal&) {
    // Simplified filtering logic
}

} // namespace IGFD

#endif // __cplusplus
