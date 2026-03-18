#ifndef IMGui_DEFINE_MATH_OPERATORS
#define IMGui_DEFINE_MATH_OPERATORS
#endif

#include "ImGuiFileDialog.h"
#include "imgui.h"
#include "imgui_internal.h"

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

#ifdef _WIN32
#define _IGFD_WIN_
#include <windows.h>
#define stat _stat
#define stricmp _stricmp
#define PATH_SEP '\\'
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#else
#define _IGFD_UNIX_
#include <sys/types.h>
#include <dirent.h>
#define stricmp strcasecmp
#define PATH_SEP '/'
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __cplusplus

namespace IGFD {

// --- Utils Implementation ---

bool Utils::ImSplitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size) {
    using namespace ImGui;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID("##Splitter");
    ImRect bb;

    ImVec2 pos1 = window->DC.CursorPos;
    ImVec2 offset = split_vertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1);
    bb.Min = ImVec2(pos1.x + offset.x, pos1.y + offset.y);

    ImVec2 size = split_vertically ? ImVec2(thickness, splitter_long_axis_size) : ImVec2(splitter_long_axis_size, thickness);
    ImVec2 item_size = CalcItemSize(size, 0.0f, 0.0f);
    bb.Max = ImVec2(bb.Min.x + item_size.x, bb.Min.y + item_size.y);

    return SplitterBehavior(bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 1.0f);
}

bool Utils::ReplaceString(std::string& str, const std::string& oldStr, const std::string& newStr, const size_t& vMaxRecursion) {
    if (str.empty() || oldStr == newStr) return false;
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

bool Utils::IsDirectoryCanBeOpened(const std::string& name) {
    if (name.empty()) return false;
#ifdef USE_STD_FILESYSTEM
    try {
        return std::filesystem::is_directory(name);
    } catch (...) { return false; }
#else
    DIR* pDir = opendir(name.c_str());
    if (pDir) { closedir(pDir); return true; }
    return false;
#endif
}

bool Utils::IsDirectoryExist(const std::string& name) {
    if (name.empty()) return false;
#ifdef USE_STD_FILESYSTEM
    return std::filesystem::is_directory(name);
#else
    DIR* pDir = opendir(name.c_str());
    if (pDir) { closedir(pDir); return true; }
    return (errno != ENOENT);
#endif
}

bool Utils::CreateDirectoryIfNotExist(const std::string& name) {
    if (name.empty() || IsDirectoryExist(name)) return false;
#ifdef USE_STD_FILESYSTEM
    return std::filesystem::create_directory(name);
#elif defined(_IGFD_WIN_)
    return CreateDirectoryA(name.c_str(), NULL) != 0;
#else
    return mkdir(name.c_str(), 0755) == 0;
#endif
}

Utils::PathStruct Utils::ParsePathFileName(const std::string& vPathFileName) {
    PathStruct res;
    if (vPathFileName.empty()) return res;
    std::string pfn = vPathFileName;
    std::replace(pfn.begin(), pfn.end(), '\\', '/');
    size_t lastSlash = pfn.find_last_of('/');
    if (lastSlash != std::string::npos) {
        res.path = pfn.substr(0, lastSlash);
        res.name = pfn.substr(lastSlash + 1);
    } else {
        res.name = pfn;
    }
    size_t lastDot = res.name.find_last_of('.');
    if (lastDot != std::string::npos) {
        res.ext = res.name.substr(lastDot + 1);
    }
    res.isOk = true;
    return res;
}

void Utils::AppendToBuffer(char* vBuffer, size_t vBufferLen, const std::string& vStr) {
    size_t slen = strlen(vBuffer);
    size_t remaining = vBufferLen - slen - 1;
    if (remaining > 0) {
        strncat(vBuffer, vStr.c_str(), remaining);
    }
}

void Utils::ResetBuffer(char* vBuffer) { vBuffer[0] = '\0'; }

void Utils::SetBuffer(char* vBuffer, size_t vBufferLen, const std::string& vStr) {
    ResetBuffer(vBuffer);
    strncpy(vBuffer, vStr.c_str(), vBufferLen - 1);
    vBuffer[vBufferLen - 1] = '\0';
}

std::string Utils::UTF8Encode(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
#ifdef _IGFD_WIN_
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
#else
    return std::string(wstr.begin(), wstr.end());
#endif
}

std::wstring Utils::UTF8Decode(const std::string& str) {
    if (str.empty()) return std::wstring();
#ifdef _IGFD_WIN_
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
#else
    return std::wstring(str.begin(), str.end());
#endif
}

std::vector<std::string> Utils::SplitStringToVector(const std::string& vText, const char& vDelimiter, const bool& vPushEmpty) {
    std::vector<std::string> res;
    std::string item;
    std::stringstream ss(vText);
    while (std::getline(ss, item, vDelimiter)) {
        if (!item.empty() || vPushEmpty) {
            res.push_back(item);
        }
    }
    return res;
}

std::vector<std::string> Utils::GetDrivesList() {
    std::vector<std::string> res;
#ifdef _IGFD_WIN_
    char drives[512];
    if (GetLogicalDriveStringsA(512, drives)) {
        char* p = drives;
        while (*p) {
            res.push_back(p);
            p += strlen(p) + 1;
        }
    }
#endif
    return res;
}

std::string Utils::LowerCaseString(const std::string& vString) {
    std::string res = vString;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c){ return std::tolower(c); });
    return res;
}

size_t Utils::GetCharCountInString(const std::string& vString, const char& vChar) {
    return std::count(vString.begin(), vString.end(), vChar);
}

size_t Utils::GetLastCharPosWithMinCharCount(const std::string& vString, const char& vChar, const size_t& vMinCharCount) {
    if (vMinCharCount == 0) return std::string::npos;
    size_t count = 0;
    for (size_t i = vString.size(); i > 0; --i) {
        if (vString[i-1] == vChar) {
            if (++count == vMinCharCount) return i - 1;
        }
    }
    return std::string::npos;
}

// --- FileDialog Implementation ---

FileDialog::FileDialog() : BookMarkFeature(), KeyExplorerFeature(), ThumbnailFeature() {
    prFileDialogInternal.puDLGkey = "";
    prFileDialogInternal.puDLGtitle = "";
    prFileDialogInternal.puShowDialog = false;
    prFileDialogInternal.puIsOk = false;
}

FileDialog::~FileDialog() {}

void FileDialog::OpenDialog(const std::string& vKey, const std::string& vTitle, const char* vFilters, const std::string& vPath, const std::string& vFileName, const int& vCountSelectionMax, UserDatas vUserDatas, ImGuiFileDialogFlags vFlags) {
    prFileDialogInternal.puDLGkey = vKey;
    prFileDialogInternal.puDLGtitle = vTitle;
    prFileDialogInternal.puFilterManager.puDLGFilters = vFilters ? vFilters : "";
    prFileDialogInternal.puFileManager.SetCurrentPath(vPath);
    prFileDialogInternal.puFileManager.SetDefaultFileName(vFileName);
    prFileDialogInternal.puFileManager.puDLGcountSelectionMax = (size_t)vCountSelectionMax;
    prFileDialogInternal.puDLGuserDatas = vUserDatas;
    prFileDialogInternal.puDLGflags = vFlags;
    prFileDialogInternal.puShowDialog = true;
    prFileDialogInternal.puIsOk = false;
}

void FileDialog::OpenDialog(const std::string& vKey, const std::string& vTitle, const char* vFilters, const std::string& vFilePathName, const int& vCountSelectionMax, UserDatas vUserDatas, ImGuiFileDialogFlags vFlags) {
    Utils::PathStruct ps = Utils::ParsePathFileName(vFilePathName);
    OpenDialog(vKey, vTitle, vFilters, ps.path, ps.name, vCountSelectionMax, vUserDatas, vFlags);
}

void FileDialog::OpenDialog(const std::string& vKey, const std::string& vTitle, const char* vFilters, const std::string& vPath, const std::string& vFileName, const PaneFun& vSidePane, const float& vSidePaneWidth, const int& vCountSelectionMax, UserDatas vUserDatas, ImGuiFileDialogFlags vFlags) {
    OpenDialog(vKey, vTitle, vFilters, vPath, vFileName, vCountSelectionMax, vUserDatas, vFlags);
    prFileDialogInternal.puDLGoptionsPane = vSidePane;
    prFileDialogInternal.puDLGoptionsPaneWidth = vSidePaneWidth;
}

void FileDialog::OpenDialog(const std::string& vKey, const std::string& vTitle, const char* vFilters, const std::string& vFilePathName, const PaneFun& vSidePane, const float& vSidePaneWidth, const int& vCountSelectionMax, UserDatas vUserDatas, ImGuiFileDialogFlags vFlags) {
    Utils::PathStruct ps = Utils::ParsePathFileName(vFilePathName);
    OpenDialog(vKey, vTitle, vFilters, ps.path, ps.name, vSidePane, vSidePaneWidth, vCountSelectionMax, vUserDatas, vFlags);
}

bool FileDialog::Display(const std::string& vKey, ImGuiWindowFlags vFlags, ImVec2 vMinSize, ImVec2 vMaxSize) {
    if (prFileDialogInternal.puShowDialog && prFileDialogInternal.puDLGkey == vKey) {
        ImGui::SetNextWindowSizeConstraints(vMinSize, vMaxSize);
        bool res = false;
        if (ImGui::Begin(prFileDialogInternal.puDLGtitle.c_str(), &prFileDialogInternal.puShowDialog, vFlags)) {
            prDrawHeader();
            prDrawContent();
            if (prDrawFooter()) {
                res = true;
            }
        }
        ImGui::End();
        return res;
    }
    return false;
}

bool FileDialog::IsOk() const { return prFileDialogInternal.puIsOk; }
void FileDialog::Close() { prFileDialogInternal.puShowDialog = false; }
bool FileDialog::IsOpened() const { return prFileDialogInternal.puShowDialog; }
bool FileDialog::IsOpened(const std::string& vKey) const { return prFileDialogInternal.puShowDialog && prFileDialogInternal.puDLGkey == vKey; }
std::string FileDialog::GetOpenedKey() const { return prFileDialogInternal.puDLGkey; }

std::string FileDialog::GetFilePathName(IGFD_ResultMode vFlag) { return prFileDialogInternal.puFileManager.GetResultingFilePathName(prFileDialogInternal, vFlag); }
std::string FileDialog::GetCurrentPath() { return prFileDialogInternal.puFileManager.GetCurrentPath(); }
std::string FileDialog::GetCurrentFileName(IGFD_ResultMode vFlag) { return prFileDialogInternal.puFileManager.puFileNameBuffer; }
std::string FileDialog::GetCurrentFilter() { return prFileDialogInternal.puFilterManager.puDLGFilters; }
UserDatas FileDialog::GetUserDatas() const { return prFileDialogInternal.puDLGuserDatas; }

// --- FileManager Implementation ---

FileManager::FileManager() {
    prCurrentPath = ".";
    Utils::SetBuffer(puInputPathBuffer, MAX_PATH_BUFFER_SIZE, prCurrentPath);
}

void FileManager::SetCurrentPath(const std::string& vPath) {
    prCurrentPath = vPath;
    if (prCurrentPath.empty()) prCurrentPath = ".";
    Utils::SetBuffer(puInputPathBuffer, MAX_PATH_BUFFER_SIZE, prCurrentPath);
}

void FileManager::SetDefaultFileName(const std::string& vFileName) {
    Utils::SetBuffer(puFileNameBuffer, MAX_FILE_DIALOG_NAME_BUFFER, vFileName);
}

std::string FileManager::GetCurrentPath() { return prCurrentPath; }

std::string FileManager::GetResultingFilePathName(FileDialogInternal& vFileDialogInternal, IGFD_ResultMode vFlag) {
    std::string path = GetCurrentPath();
    if (!path.empty() && path.back() != PATH_SEP) path += PATH_SEP;
    return path + std::string(puFileNameBuffer);
}

bool FileManager::SetPathOnParentDirectoryIfAny() {
    size_t lastSlash = prCurrentPath.find_last_of(PATH_SEP);
    if (lastSlash != std::string::npos && lastSlash > 0) {
        SetCurrentPath(prCurrentPath.substr(0, lastSlash));
        return true;
    }
    return false;
}

void FileManager::ApplyFilteringOnFileList(const FileDialogInternal& vFileDialogInternal) {}

void FileManager::ApplyFilteringOnFileList(const FileDialogInternal& vFileDialogInternal, std::vector<std::shared_ptr<FileInfos>>& vFileInfosList, std::vector<std::shared_ptr<FileInfos>>& vFileInfosFilteredList) {}

// --- FilterManager Implementation ---

// --- FileType Implementation ---
FileType::FileType() : m_Content(ContentType::Invalid), m_Symlink(false) {}
FileType::FileType(const ContentType& vContentType, const bool& vIsSymlink) : m_Content(vContentType), m_Symlink(vIsSymlink) {}
void FileType::SetContent(const ContentType& vContentType) { m_Content = vContentType; }
void FileType::SetSymLink(const bool& vIsSymlink) { m_Symlink = vIsSymlink; }
bool FileType::isValid() const { return m_Content != ContentType::Invalid; }
bool FileType::isDir() const { return m_Content == ContentType::Directory; }
bool FileType::isFile() const { return m_Content == ContentType::File; }
bool FileType::isLinkToUnknown() const { return m_Content == ContentType::LinkToUnknown; }
bool FileType::isSymLink() const { return m_Symlink; }
bool FileType::operator==(const FileType& rhs) const { return m_Content == rhs.m_Content; }
bool FileType::operator!=(const FileType& rhs) const { return m_Content != rhs.m_Content; }
bool FileType::operator<(const FileType& rhs) const { return (int)m_Content < (int)rhs.m_Content; }
bool FileType::operator>(const FileType& rhs) const { return (int)m_Content > (int)rhs.m_Content; }

// --- Feature Implementations ---
KeyExplorerFeature::KeyExplorerFeature() {}
BookMarkFeature::BookMarkFeature() {}
ThumbnailFeature::ThumbnailFeature() {}
ThumbnailFeature::~ThumbnailFeature() {}

void FileDialog::prDrawHeader() {
    if (ImGui::Button("..")) {
        prFileDialogInternal.puFileManager.SetPathOnParentDirectoryIfAny();
    }
    ImGui::SameLine();
    ImGui::Text("Path: %s", prFileDialogInternal.puFileManager.GetCurrentPath().c_str());
}

void FileDialog::prDrawContent() {
    prDrawFileListView(ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2));
}

bool FileDialog::prDrawFooter() {
    ImGui::InputText("File", prFileDialogInternal.puFileManager.puFileNameBuffer, MAX_FILE_DIALOG_NAME_BUFFER);
    return prDrawValidationButtons();
}

void FileDialog::DisplayPathPopup(ImVec2 vSize) {}

bool FileDialog::prDrawValidationButtons() {
    bool res = false;
    if (prDrawOkButton()) {
        prFileDialogInternal.puIsOk = true;
        prFileDialogInternal.puShowDialog = false;
        res = true;
    }
    ImGui::SameLine();
    if (prDrawCancelButton()) {
        prFileDialogInternal.puIsOk = false;
        prFileDialogInternal.puShowDialog = false;
        res = true;
    }
    return res;
}

bool FileDialog::prDrawOkButton() {
    return ImGui::Button("OK");
}

bool FileDialog::prDrawCancelButton() {
    return ImGui::Button("Cancel");
}

void FileDialog::prDrawSidePane(float vHeight) {}

void FileDialog::prSelectableItem(int vidx, std::shared_ptr<FileInfos> vInfos, bool vSelected, const char* vFmt, ...) {}

void FileDialog::prDrawFileListView(ImVec2 vSize) {
    ImGui::BeginChild("FileList", vSize, true);
    ImGui::EndChild();
}

} // namespace IGFD

#endif // __cplusplus
