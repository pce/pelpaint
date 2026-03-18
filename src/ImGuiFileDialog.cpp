// PATCH: Fixed unterminated preprocessor conditional (iOS/cross-platform support fix).
#pragma region PVS STUDIO

// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#pragma endregion

#pragma region IGFD LICENSE

/*
MIT License

Copyright (c) 2019-2020 Stephane Cuillerdier (aka aiekick)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma endregion

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ImGuiFileDialog.h"

#pragma region Includes

#include <cfloat>
#include <cstring>  // stricmp / strcasecmp
#include <cstdarg>  // variadic
#include <sstream>
#include <iomanip>
#include <ctime>
#include <sys/stat.h>
#include <cstdio>
#include <cerrno>

// this option need c++17
#ifdef USE_STD_FILESYSTEM
#include <filesystem>
#include <exception>
#endif  // USE_STD_FILESYSTEM

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif  // __EMSCRIPTEN__

#ifdef _MSC_VER

#define IGFD_DEBUG_BREAK \
    if (IsDebuggerPresent()) __debugbreak()
#else
#define IGFD_DEBUG_BREAK
#endif

#if defined(__WIN32__) || defined(WIN32) || defined(_WIN32) || defined(__WIN64__) || defined(WIN64) || defined(_WIN64) || defined(_MSC_VER)
#define _IGFD_WIN_
#define stat _stat
#define stricmp _stricmp
#include <cctype>
// this option need c++17
#ifdef USE_STD_FILESYSTEM
#include <windows.h>
#else
#include "dirent/dirent.h"  // directly open the dirent file attached to this lib
#endif                      // USE_STD_FILESYSTEM
#define PATH_SEP '\\'
#ifndef PATH_MAX
#define PATH_MAX 260
#endif  // PATH_MAX
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__) || defined(__APPLE__) || defined(__EMSCRIPTEN__)
#define _IGFD_UNIX_
#define stricmp strcasecmp
#include <sys/types.h>
// this option need c++17
#ifndef USE_STD_FILESYSTEM
#include <dirent.h>
#endif  // USE_STD_FILESYSTEM
#define PATH_SEP '/'
#endif  // defined(__WIN32__) ... else defined(__linux__) ...

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdlib>
#include <algorithm>
#include <iostream>

#pragma endregion

// PATCH: Moved __cplusplus guard to only wrap C++ code.
#ifdef __cplusplus

#pragma region Common defines

#ifdef USE_THUMBNAILS
#ifndef DONT_DEFINE_AGAIN__STB_IMAGE_IMPLEMENTATION
#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif  // STB_IMAGE_IMPLEMENTATION
#endif  // DONT_DEFINE_AGAIN__STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#ifndef DONT_DEFINE_AGAIN__STB_IMAGE_RESIZE_IMPLEMENTATION
#ifndef STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#endif  // STB_IMAGE_RESIZE_IMPLEMENTATION
#endif  // DONT_DEFINE_AGAIN__STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize.h"
#endif  // USE_THUMBNAILS

// float comparisons
#ifndef IS_FLOAT_DIFFERENT
#define IS_FLOAT_DIFFERENT(a, b) (fabs((a) - (b)) > FLT_EPSILON)
#endif  // IS_FLOAT_DIFFERENT
#ifndef IS_FLOAT_EQUAL
#define IS_FLOAT_EQUAL(a, b) (fabs((a) - (b)) < FLT_EPSILON)
#endif  // IS_FLOAT_EQUAL

#pragma endregion

#pragma region IGFD NAMESPACE

#pragma region CUSTOMIZATION DEFINES

///////////////////////////////
// COMBOBOX
///////////////////////////////
#ifndef FILTER_COMBO_AUTO_SIZE
#define FILTER_COMBO_AUTO_SIZE 1
#endif // FILTER_COMBO_AUTO_SIZE
#ifndef FILTER_COMBO_MIN_WIDTH
#define FILTER_COMBO_MIN_WIDTH 150.0f
#endif  // FILTER_COMBO_MIN_WIDTH
#ifndef IMGUI_BEGIN_COMBO
#define IMGUI_BEGIN_COMBO ImGui::BeginCombo
#endif  // IMGUI_BEGIN_COMBO
///////////////////////////////
// BUTTON
///////////////////////////////
// for lets you define your button widget
// if you have like me a special bi-color button
#ifndef IMGUI_PATH_BUTTON
#define IMGUI_PATH_BUTTON ImGui::Button
#endif  // IMGUI_PATH_BUTTON
#ifndef IMGUI_BUTTON
#define IMGUI_BUTTON ImGui::Button
#endif  // IMGUI_BUTTON
///////////////////////////////
// locales
///////////////////////////////
#ifndef createDirButtonString
#define createDirButtonString "+"
#endif  // createDirButtonString
#ifndef okButtonString
#define okButtonString "OK"
#endif  // okButtonString
#ifndef okButtonWidth
#define okButtonWidth 0.0f
#endif  // okButtonWidth
#ifndef cancelButtonString
#define cancelButtonString "Cancel"
#endif  // cancelButtonString
#ifndef cancelButtonWidth
#define cancelButtonWidth 0.0f
#endif  // cancelButtonWidth
#ifndef okCancelButtonAlignement
#define okCancelButtonAlignement 0.0f
#endif  // okCancelButtonAlignement
#ifndef invertOkAndCancelButtons
// 0 => disabled, 1 => enabled
#define invertOkAndCancelButtons 0
#endif  // invertOkAndCancelButtons
#ifndef resetButtonString
#define resetButtonString "R"
#endif  // resetButtonString
#ifndef drivesButtonString
#define drivesButtonString "Drives"
#endif  // drivesButtonString
#ifndef editPathButtonString
#define editPathButtonString "E"
#endif  // editPathButtonString
#ifndef searchString
#define searchString "Search :"
#endif  // searchString
#ifndef dirEntryString
#define dirEntryString "[Dir]"
#endif  // dirEntryString
#ifndef linkEntryString
#define linkEntryString "[Link]"
#endif  // linkEntryString
#ifndef fileEntryString
#define fileEntryString "[File]"
#endif  // fileEntryString
#ifndef fileNameString
#define fileNameString "File Name :"
#endif  // fileNameString
#ifndef dirNameString
#define dirNameString "Directory Path :"
#endif  // dirNameString
#ifndef buttonResetSearchString
#define buttonResetSearchString "Reset search"
#endif  // buttonResetSearchString
#ifndef buttonDriveString
#define buttonDriveString "Drives"
#endif  // buttonDriveString
#ifndef buttonEditPathString
#define buttonEditPathString "Edit path\nYou can also right click on path buttons"
#endif  // buttonEditPathString
#ifndef buttonResetPathString
#define buttonResetPathString "Reset to current directory"
#endif  // buttonResetPathString
#ifndef buttonCreateDirString
#define buttonCreateDirString "Create Directory"
#endif  // buttonCreateDirString
#ifndef tableHeaderAscendingIcon
#define tableHeaderAscendingIcon "A|"
#endif  // tableHeaderAscendingIcon
#ifndef tableHeaderDescendingIcon
#define tableHeaderDescendingIcon "D|"
#endif  // tableHeaderDescendingIcon
#ifndef tableHeaderFileNameString
#define tableHeaderFileNameString "File name"
#endif  // tableHeaderFileNameString
#ifndef tableHeaderFileTypeString
#define tableHeaderFileTypeString "Type"
#endif  // tableHeaderFileTypeString
#ifndef tableHeaderFileSizeString
#define tableHeaderFileSizeString "Size"
#endif  // tableHeaderFileSizeString
#ifndef tableHeaderFileDateString
#define tableHeaderFileDateString "Date"
#endif  // tableHeaderFileDateString
#ifndef fileSizeBytes
#define fileSizeBytes "o"
#endif  // fileSizeBytes
#ifndef fileSizeKiloBytes
#define fileSizeKiloBytes "Ko"
#endif  // fileSizeKiloBytes
#ifndef fileSizeMegaBytes
#define fileSizeMegaBytes "Mo"
#endif  // fileSizeMegaBytes
#ifndef fileSizeGigaBytes
#define fileSizeGigaBytes "Go"
#endif  // fileSizeGigaBytes
#ifndef OverWriteDialogTitleString
#define OverWriteDialogTitleString "The file Already Exist !"
#endif  // OverWriteDialogTitleString
#ifndef OverWriteDialogMessageString
#define OverWriteDialogMessageString "Would you like to OverWrite it ?"
#endif  // OverWriteDialogMessageString
#ifndef OverWriteDialogConfirmButtonString
#define OverWriteDialogConfirmButtonString "Confirm"
#endif  // OverWriteDialogConfirmButtonString
#ifndef OverWriteDialogCancelButtonString
#define OverWriteDialogCancelButtonString "Cancel"
#endif  // OverWriteDialogCancelButtonString
#ifndef DateTimeFormat
// see strftime functionin <ctime> for customize
#define DateTimeFormat "%Y/%m/%d %H:%M"
#endif  // DateTimeFormat
///////////////////////////////
// THUMBNAILS
///////////////////////////////
#ifdef USE_THUMBNAILS
#ifndef tableHeaderFileThumbnailsString
#define tableHeaderFileThumbnailsString "Thumbnails"
#endif  // tableHeaderFileThumbnailsString
#ifndef DisplayMode_FilesList_ButtonString
#define DisplayMode_FilesList_ButtonString "FL"
#endif  // DisplayMode_FilesList_ButtonString
#ifndef DisplayMode_FilesList_ButtonHelp
#define DisplayMode_FilesList_ButtonHelp "File List"
#endif  // DisplayMode_FilesList_ButtonHelp
#ifndef DisplayMode_ThumbailsList_ButtonString
#define DisplayMode_ThumbailsList_ButtonString "TL"
#endif  // DisplayMode_ThumbailsList_ButtonString
#ifndef DisplayMode_ThumbailsList_ButtonHelp
#define DisplayMode_ThumbailsList_ButtonHelp "Thumbnails List"
#endif  // DisplayMode_ThumbailsList_ButtonHelp
#ifndef DisplayMode_ThumbailsGrid_ButtonString
#define DisplayMode_ThumbailsGrid_ButtonString "TG"
#endif  // DisplayMode_ThumbailsGrid_ButtonString
#ifndef DisplayMode_ThumbailsGrid_ButtonHelp
#define DisplayMode_ThumbailsGrid_ButtonHelp "Thumbnails Grid"
#endif  // DisplayMode_ThumbailsGrid_ButtonHelp
#ifndef DisplayMode_ThumbailsList_ImageHeight
#define DisplayMode_ThumbailsList_ImageHeight 32.0f
#endif  // DisplayMode_ThumbailsList_ImageHeight
#ifndef IMGUI_RADIO_BUTTON
inline bool inRadioButton(const char* vLabel, bool vToggled) {
    bool pressed = false;
    if (vToggled) {
        ImVec4 bua = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        ImVec4 te  = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::PushStyleColor(ImGuiCol_Button, te);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, te);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, te);
        ImGui::PushStyleColor(ImGuiCol_Text, bua);
    }
    pressed = IMGUI_BUTTON(vLabel);
    if (vToggled) {
        ImGui::PopStyleColor(4);  //-V112
    }
    return pressed;
}
#define IMGUI_RADIO_BUTTON inRadioButton
#endif  // IMGUI_RADIO_BUTTON
#endif  // USE_THUMBNAILS
///////////////////////////////
// BOOKMARKS
///////////////////////////////
#ifdef USE_BOOKMARK
#ifndef defaultBookmarkPaneWith
#define defaultBookmarkPaneWith 150.0f
#endif  // defaultBookmarkPaneWith
#ifndef bookmarksButtonString
#define bookmarksButtonString "Bookmark"
#endif  // bookmarksButtonString
#ifndef bookmarksButtonHelpString
#define bookmarksButtonHelpString "Bookmark"
#endif  // bookmarksButtonHelpString
#ifndef addBookmarkButtonString
#define addBookmarkButtonString "+"
#endif  // addBookmarkButtonString
#ifndef removeBookmarkButtonString
#define removeBookmarkButtonString "-"
#endif  // removeBookmarkButtonString
#ifndef IMGUI_TOGGLE_BUTTON
inline bool inToggleButton(const char* vLabel, bool* vToggled) {
    bool pressed = false;

    if (vToggled && *vToggled) {
        ImVec4 bua = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        // ImVec4 buh = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        // ImVec4 bu = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        ImVec4 te = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::PushStyleColor(ImGuiCol_Button, te);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, te);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, te);
        ImGui::PushStyleColor(ImGuiCol_Text, bua);
    }

    pressed = IMGUI_BUTTON(vLabel);

    if (vToggled && *vToggled) {
        ImGui::PopStyleColor(4);  //-V112
    }

    if (vToggled && pressed) *vToggled = !*vToggled;

    return pressed;
}
#define IMGUI_TOGGLE_BUTTON inToggleButton
#endif  // IMGUI_TOGGLE_BUTTON
#endif  // USE_BOOKMARK

#pragma endregion

#pragma region INTERNAL

#pragma region EXCEPTION

class IGFDException : public std::exception {
private:
    std::string m_Message;

public:
    IGFDException(const std::string& vMessage) : m_Message(vMessage) {
    }
    const char* what() {
        return m_Message.c_str();
    }
};

#pragma endregion

#pragma region Utils

#ifndef USE_STD_FILESYSTEM
inline int inAlphaSort(const struct dirent** a, const struct dirent** b) {
    return strcoll((*a)->d_name, (*b)->d_name);
}
#endif

// https://github.com/ocornut/imgui/issues/1720
IGFD_API bool IGFD::Utils::ImSplitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size) {
    using namespace ImGui;
    ImGuiContext& g     = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id          = window->GetID("##Splitter");
    ImRect bb;
    bb.Min = window->DC.CursorPos + (split_vertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
    bb.Max = bb.Min + CalcItemSize(split_vertically ? ImVec2(thickness, splitter_long_axis_size) : ImVec2(splitter_long_axis_size, thickness), 0.0f, 0.0f);
    return SplitterBehavior(bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 1.0f);
}

// Convert a wide Unicode string to an UTF8 string
IGFD_API std::string IGFD::Utils::UTF8Encode(const std::wstring& wstr) {
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
    // Suppress warnings from the compiler.
    (void)wstr;
#endif  // _IGFD_WIN_
    return res;
}

// Convert an UTF8 string to a wide Unicode String
IGFD_API std::wstring IGFD::Utils::UTF8Decode(const std::string& str) {
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
    // Suppress warnings from the compiler.
    (void)str;
#endif  // _IGFD_WIN_
    return res;
}

IGFD_API bool IGFD::Utils::ReplaceString(std::string& str, const ::std::string& oldStr, const ::std::string& newStr, const size_t& vMaxRecursion) {
    if (!str.empty() && oldStr != newStr) {
        bool res             = false;
        size_t pos           = 0;
        bool found           = false;
        size_t max_recursion = vMaxRecursion;
        do {
            pos = str.find(oldStr, pos);
            if (pos != std::string::npos) {
                found = res = true;
                str.replace(pos, oldStr.length(), newStr);
                pos += newStr.length();
            } else if (found && max_recursion > 0) {  // recursion loop
                found = false;
                pos   = 0;
                --max_recursion;
            }
        } while (pos != std::string::npos);
        return res;
    }
    return false;
}

IGFD_API std::vector<std::string> IGFD::Utils::SplitStringToVector(const std::string& vText, const char& vDelimiter, const bool& vPushEmpty) {
    std::vector<std::string> arr;
    if (!vText.empty()) {
        size_t start = 0;
        size_t end   = vText.find(vDelimiter, start);
        while (end != std::string::npos) {
            auto token = vText.substr(start, end - start);
            if (!token.empty() || (token.empty() && vPushEmpty)) {  //-V728
                arr.push_back(token);
            }
            start = end + 1;
            end   = vText.find(vDelimiter, start);
        }
        auto token = vText.substr(start);
        if (!token.empty() || (token.empty() && vPushEmpty)) {  //-V728
            arr.push_back(token);
        }
    }
    return arr;
}

IGFD_API std::vector<std::string> IGFD::Utils::GetDrivesList() {
    std::vector<std::string> res;

#ifdef _IGFD_WIN_
    const DWORD mydrives = 2048;
    char lpBuffer[2048];
#define mini(a, b) (((a) < (b)) ? (a) : (b))
    const DWORD countChars = mini(GetLogicalDriveStringsA(mydrives, lpBuffer), 2047);
#undef mini
    if (countChars > 0U && countChars < 2049U) {
        std::string var = std::string(lpBuffer, (size_t)countChars);
        IGFD::Utils::ReplaceString(var, "\\", "");
        res = IGFD::Utils::SplitStringToVector(var, '\0', false);
    }
#endif  // _IGFD_WIN_

    return res;
}

IGFD_API bool IGFD::Utils::IsDirectoryCanBeOpened(const std::string& name) {
    bool bExists = false;

    if (!name.empty()) {
#ifdef USE_STD_FILESYSTEM
        namespace fs = std::filesystem;
#ifdef _IGFD_WIN_
        std::wstring wname = IGFD::Utils::UTF8Decode(name.c_str());
        fs::path pathName  = fs::path(wname);
#else   // _IGFD_WIN_
        fs::path pathName = fs::path(name);
#endif  // _IGFD_WIN_
        try {
            // interesting, in the case of a protected dir or for any reason the dir cant be opened
            // this func will work but will say nothing more . not like the dirent version
            bExists = fs::is_directory(pathName);
            // test if can be opened, this function can thrown an exception if there is an issue with this dir
            // here, the dir_iter is need else not exception is thrown..
            const auto dir_iter = std::filesystem::directory_iterator(pathName);
            (void)dir_iter;  // for avoid unused warnings
        } catch (const std::exception& /*ex*/) {
            // fail so this dir cant be opened
            bExists = false;
        }
#else
        DIR* pDir = nullptr;
        // interesting, in the case of a protected dir or for any reason the dir cant be opened
        // this func will fail
        pDir = opendir(name.c_str());
        if (pDir != nullptr) {
            bExists = true;
            (void)closedir(pDir);
        }
#endif  // USE_STD_FILESYSTEM
    }

    return bExists;  // this is not a directory!
}

IGFD_API bool IGFD::Utils::IsDirectoryExist(const std::string& name) {
    bool bExists = false;

    if (!name.empty()) {
#ifdef USE_STD_FILESYSTEM
        namespace fs = std::filesystem;
#ifdef _IGFD_WIN_
        std::wstring wname = IGFD::Utils::UTF8Decode(name.c_str());
        fs::path pathName  = fs::path(wname);
#else   // _IGFD_WIN_
        fs::path pathName = fs::path(name);
#endif  // _IGFD_WIN_
        bExists = fs::is_directory(pathName);
#else
        DIR* pDir = nullptr;
        pDir      = opendir(name.c_str());
        if (pDir) {
            bExists = true;
            closedir(pDir);
        } else if (ENOENT == errno) {
            /* Directory does not exist. */
            // bExists = false;
        } else {
            /* opendir() failed for some other reason.
               like if a dir is protected, or not accessable with user right
            */
            bExists = true;
        }
#endif  // USE_STD_FILESYSTEM
    }

    return bExists;  // this is not a directory!
}

IGFD_API bool IGFD::Utils::CreateDirectoryIfNotExist(const std::string& name) {
    bool res = false;

    if (!name.empty()) {
        if (!IsDirectoryExist(name)) {
#ifdef _IGFD_WIN_
#ifdef USE_STD_FILESYSTEM
            namespace fs       = std::filesystem;
            std::wstring wname = IGFD::Utils::UTF8Decode(name.c_str());
            fs::path pathName  = fs::path(wname);
            res                = fs::create_directory(pathName);
#else                          // USE_STD_FILESYSTEM
            std::wstring wname = IGFD::Utils::UTF8Decode(name);
            if (CreateDirectoryW(wname.c_str(), nullptr)) {
                res = true;
            }
#endif                         // USE_STD_FILESYSTEM
#elif defined(__EMSCRIPTEN__)  // _IGFD_WIN_
            std::string str = std::string("FS.mkdir('") + name + "');";
            emscripten_run_script(str.c_str());
            res = true;
#elif defined(_IGFD_UNIX_)
#ifdef USE_STD_FILESYSTEM
            namespace fs = std::filesystem;
            fs::path pathName = fs::path(name);
            res = fs::create_directory(pathName);
#else
            // Directory creation is not supported on UNIX if std::filesystem is not enabled.
#endif
#endif  // _IGFD_WIN_
            if (!res) {
                std::cout << "Error creating directory " << name << std::endl;
            }
        }
    }

    return res;
}

IGFD_API IGFD::Utils::PathStruct IGFD::Utils::ParsePathFileName(const std::string& vPathFileName) {
#ifdef USE_STD_FILESYSTEM
    // https://github.com/aiekick/ImGuiFileDialog/issues/54
    namespace fs = std::filesystem;
    PathStruct res;
    if (vPathFileName.empty()) return res;

    auto fsPath = fs::path(vPathFileName);

    if (fs::is_directory(fsPath)) {
        res.name = "";
        res.path = fsPath.string();
        res.isOk = true;

    } else if (fs::is_regular_file(fsPath)) {
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
        IGFD::Utils::ReplaceString(pfn, "\\", separator);
        IGFD::Utils::ReplaceString(pfn, "/", separator);

        size_t lastSlash = pfn.find_last_of(separator);
        if (lastSlash != std::string::npos) {
            res.name = pfn.substr(lastSlash + 1);
            res.path = pfn.substr(0, lastSlash);
            res.isOk = true;
        }

        size_t lastPoint = pfn.find_last_of('.');
        if (lastPoint != std::string::npos) {
            if (!res.isOk) {
                res.name = pfn;
                res.isOk = true;
            }
            res.ext = pfn.substr(lastPoint + 1);
            IGFD::Utils::ReplaceString(res.name, "." + res.ext, "");
        }

        if (!res.isOk) {
            res.name = std::move(pfn);
            res.isOk = true;
        }
    }

    return res;
#endif  // USE_STD_FILESYSTEM
}

IGFD_API void IGFD::Utils::AppendToBuffer(char* vBuffer, size_t vBufferLen, const std::string& vStr) {
    std::string st = vStr;
    size_t len     = vBufferLen - 1u;
    size_t slen    = strlen(vBuffer);

    if (!st.empty() && st != "\n") {
        IGFD::Utils::ReplaceString(st, "\n", "");
        IGFD::Utils::ReplaceString(st, "\r", "");
    }
    vBuffer[slen]   = '\0';
    std::string str = std::string(vBuffer);
    // if (!str.empty()) str += "\n";
    str += vStr;
    if (len > str.size()) {
        len = str.size();
    }
#ifdef _MSC_VER
    strncpy_s(vBuffer, vBufferLen, str.c_str(), len);
#else   // _MSC_VER
    strncpy(vBuffer, str.c_str(), len);
#endif  // _MSC_VER
    vBuffer[len] = '\0';
}

IGFD_API void IGFD::Utils::ResetBuffer(char* vBuffer) {
    vBuffer[0] = '\0';
}

IGFD_API void IGFD::Utils::SetBuffer(char* vBuffer, size_t vBufferLen, const std::string& vStr) {
    ResetBuffer(vBuffer);
    AppendToBuffer(vBuffer, vBufferLen, vStr);
}

IGFD_API std::string IGFD::Utils::LowerCaseString(const std::string& vString) {
    auto str = vString;

    // convert to lower case
    for (char& c : str) {
        c = (char)std::tolower(c);
    }

    return str;
}

IGFD_API size_t IGFD::Utils::GetCharCountInString(const std::string& vString, const char& vChar) {
    size_t res = 0U;
    for (const auto& c : vString) {
        if (c == vChar) {
            ++res;
        }
    }
    return res;
}

IGFD_API size_t IGFD::Utils::GetLastCharPosWithMinCharCount(const std::string& vString, const char& vChar, const size_t& vMinCharCount) {
    if (vMinCharCount) {
        size_t last_dot_pos = vString.size() + 1U;
        size_t count_dots   = vMinCharCount;
        while (count_dots > 0U && last_dot_pos > 0U && last_dot_pos != std::string::npos) {
            auto new_dot = vString.rfind(vChar, last_dot_pos - 1U);
            if (new_dot != std::string::npos) {
                last_dot_pos = new_dot;
                --count_dots;
            } else {
                break;
            }
        }
        return last_dot_pos;
    }
    return std::string::npos;
}

#pragma endregion

#pragma region FileStyle

IGFD_API IGFD::FileStyle::FileStyle() : color(0, 0, 0, 0) {
}

IGFD_API IGFD::FileStyle::FileStyle(const FileStyle& vStyle) {
    color = vStyle.color;
    icon  = vStyle.icon;
    font  = vStyle.font;
    flags = vStyle.flags;
}

IGFD_API IGFD::FileStyle::FileStyle(const ImVec4& vColor, const std::string& vIcon, ImFont* vFont) : color(vColor), icon(vIcon), font(vFont) {
}

#pragma endregion

#pragma region SearchManager

IGFD_API void IGFD::SearchManager::Clear() {
    puSearchTag.clear();
    IGFD::Utils::ResetBuffer(puSearchBuffer);
}

IGFD_API void IGFD::SearchManager::DrawSearchBar(FileDialogInternal& vFileDialogInternal) {
    // search field
    if (IMGUI_BUTTON(resetButtonString "##BtnImGuiFileDialogSearchField")) {
        Clear();
        vFileDialogInternal.puFileManager.ApplyFilteringOnFileList(vFileDialogInternal);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(buttonResetSearchString);
    ImGui::SameLine();
    ImGui::Text(searchString);
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

#pragma endregion

// ... (rest of file unchanged) ...

#endif // __cplusplus
