#include "AppSettings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
extern "C" char* iOS_GetDocumentsPath();
#endif

namespace pelpaint {

// ---------------------------------------------------------------------------
// Key name table — every key a user might reasonably bind.
// Names are lowercase and match the serialized INI form.
// ---------------------------------------------------------------------------
namespace {

struct KeyEntry { ImGuiKey key; std::string_view name; };

static constexpr std::array kKeyTable {
    // Letters
    KeyEntry{ImGuiKey_A,"a"}, KeyEntry{ImGuiKey_B,"b"}, KeyEntry{ImGuiKey_C,"c"},
    KeyEntry{ImGuiKey_D,"d"}, KeyEntry{ImGuiKey_E,"e"}, KeyEntry{ImGuiKey_F,"f"},
    KeyEntry{ImGuiKey_G,"g"}, KeyEntry{ImGuiKey_H,"h"}, KeyEntry{ImGuiKey_I,"i"},
    KeyEntry{ImGuiKey_J,"j"}, KeyEntry{ImGuiKey_K,"k"}, KeyEntry{ImGuiKey_L,"l"},
    KeyEntry{ImGuiKey_M,"m"}, KeyEntry{ImGuiKey_N,"n"}, KeyEntry{ImGuiKey_O,"o"},
    KeyEntry{ImGuiKey_P,"p"}, KeyEntry{ImGuiKey_Q,"q"}, KeyEntry{ImGuiKey_R,"r"},
    KeyEntry{ImGuiKey_S,"s"}, KeyEntry{ImGuiKey_T,"t"}, KeyEntry{ImGuiKey_U,"u"},
    KeyEntry{ImGuiKey_V,"v"}, KeyEntry{ImGuiKey_W,"w"}, KeyEntry{ImGuiKey_X,"x"},
    KeyEntry{ImGuiKey_Y,"y"}, KeyEntry{ImGuiKey_Z,"z"},
    // Digits
    KeyEntry{ImGuiKey_0,"0"}, KeyEntry{ImGuiKey_1,"1"}, KeyEntry{ImGuiKey_2,"2"},
    KeyEntry{ImGuiKey_3,"3"}, KeyEntry{ImGuiKey_4,"4"}, KeyEntry{ImGuiKey_5,"5"},
    KeyEntry{ImGuiKey_6,"6"}, KeyEntry{ImGuiKey_7,"7"}, KeyEntry{ImGuiKey_8,"8"},
    KeyEntry{ImGuiKey_9,"9"},
    // Function keys
    KeyEntry{ImGuiKey_F1,"f1"},  KeyEntry{ImGuiKey_F2,"f2"},  KeyEntry{ImGuiKey_F3,"f3"},
    KeyEntry{ImGuiKey_F4,"f4"},  KeyEntry{ImGuiKey_F5,"f5"},  KeyEntry{ImGuiKey_F6,"f6"},
    KeyEntry{ImGuiKey_F7,"f7"},  KeyEntry{ImGuiKey_F8,"f8"},  KeyEntry{ImGuiKey_F9,"f9"},
    KeyEntry{ImGuiKey_F10,"f10"},KeyEntry{ImGuiKey_F11,"f11"},KeyEntry{ImGuiKey_F12,"f12"},
    // Special keys
    KeyEntry{ImGuiKey_Space,       "space"},
    KeyEntry{ImGuiKey_Enter,       "enter"},
    KeyEntry{ImGuiKey_Escape,      "escape"},
    KeyEntry{ImGuiKey_Delete,      "delete"},
    KeyEntry{ImGuiKey_Backspace,   "backspace"},
    KeyEntry{ImGuiKey_Tab,         "tab"},
    KeyEntry{ImGuiKey_Equal,       "="},
    KeyEntry{ImGuiKey_Minus,       "-"},
    KeyEntry{ImGuiKey_LeftBracket, "["},
    KeyEntry{ImGuiKey_RightBracket,"]"},
    KeyEntry{ImGuiKey_Semicolon,   ";"},
    KeyEntry{ImGuiKey_Apostrophe,  "'"},
    KeyEntry{ImGuiKey_Comma,       ","},
    KeyEntry{ImGuiKey_Period,      "."},
    KeyEntry{ImGuiKey_Slash,       "/"},
    KeyEntry{ImGuiKey_Backslash,   "\\"},
    KeyEntry{ImGuiKey_GraveAccent, "`"},
    // Arrow keys
    KeyEntry{ImGuiKey_UpArrow,    "up"},
    KeyEntry{ImGuiKey_DownArrow,  "down"},
    KeyEntry{ImGuiKey_LeftArrow,  "left"},
    KeyEntry{ImGuiKey_RightArrow, "right"},
};

[[nodiscard]] static ImGuiKey NameToKey(std::string_view name) noexcept {
    for (const auto& [k, n] : kKeyTable)
        if (n == name) return k;
    return ImGuiKey_None;
}

[[nodiscard]] static std::string_view KeyToName(ImGuiKey key) noexcept {
    for (const auto& [k, n] : kKeyTable)
        if (k == key) return n;
    return {};
}

// ---------------------------------------------------------------------------
// Tiny INI parser
// ---------------------------------------------------------------------------
using Section = std::unordered_map<std::string, std::string>;
using IniMap  = std::unordered_map<std::string, Section>;

[[nodiscard]] static IniMap ParseIni(std::istream& in) {
    IniMap   result;
    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        line = line.substr(s);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            const auto e = line.find(']');
            if (e != std::string::npos) section = line.substr(1, e - 1);
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        while (!k.empty() && std::isspace(static_cast<unsigned char>(k.back()))) k.pop_back();
        const auto vs = v.find_first_not_of(" \t");
        if (vs != std::string::npos) v = v.substr(vs);
        result[section][k] = v;
    }
    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// KeyBinding
// ---------------------------------------------------------------------------
bool KeyBinding::JustPressed() const noexcept {
    if (key == ImGuiKey_None) return false;
    const ImGuiIO& io = ImGui::GetIO();
    if (ctrl  != io.KeyCtrl)  return false;
    if (shift != io.KeyShift) return false;
    if (alt   != io.KeyAlt)   return false;
    return ImGui::IsKeyPressed(key, /*repeat=*/false);
}

std::string KeyBinding::Display() const {
    if (key == ImGuiKey_None) return "(none)";
    std::string s;
    if (ctrl)  s += "Ctrl+";
    if (shift) s += "Shift+";
    if (alt)   s += "Alt+";
    auto name = KeyToName(key);
    if (name.empty()) {
        s += '?';
    } else {
        std::string n(name);
        if (!n.empty())
            n[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(n[0])));
        s += n;
    }
    return s;
}

std::string KeyBinding::Serialize() const {
    if (key == ImGuiKey_None) return {};
    std::string s;
    if (ctrl)  s += "ctrl+";
    if (shift) s += "shift+";
    if (alt)   s += "alt+";
    s += std::string(KeyToName(key));
    return s;
}

KeyBinding KeyBinding::Deserialize(std::string_view raw) {
    if (raw.empty()) return {};
    std::string s(raw);
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    KeyBinding kb;
    auto eat = [&](std::string_view prefix) -> bool {
        if (s.starts_with(prefix)) { s.erase(0, prefix.size()); return true; }
        return false;
    };
    // Accept modifiers in any order
    bool progress = true;
    while (progress) {
        progress  = (eat("ctrl+")  && (kb.ctrl  = true));
        progress |= (eat("shift+") && (kb.shift = true));
        progress |= (eat("alt+")   && (kb.alt   = true));
    }
    kb.key = NameToKey(s);
    return kb;
}

// ---------------------------------------------------------------------------
// AppSettings
// ---------------------------------------------------------------------------
AppSettings AppSettings::Defaults() noexcept { return {}; }

std::string AppSettings::DefaultPath() {
#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
    char* docs = iOS_GetDocumentsPath();
    std::string p = docs ? (std::string(docs) + "/pelpaint_settings.ini")
                         : "pelpaint_settings.ini";
    free(docs);
    return p;
#elif defined(_WIN32)
    if (const char* a = std::getenv("APPDATA"))
        return std::string(a) + "\\pelpaint\\settings.ini";
    return "pelpaint_settings.ini";
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME"))
        return std::string(h) + "/Library/Application Support/pelpaint/settings.ini";
    return "pelpaint_settings.ini";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME"))
        return std::string(x) + "/pelpaint/settings.ini";
    if (const char* h = std::getenv("HOME"))
        return std::string(h) + "/.config/pelpaint/settings.ini";
    return "pelpaint_settings.ini";
#endif
}

AppSettings AppSettings::Load(const std::string& path) {
    AppSettings s = Defaults();
    std::ifstream f(path);
    if (!f.is_open()) return s;
    const IniMap ini = ParseIni(f);

    auto get = [&](const char* section, const char* key) -> std::string {
        auto si = ini.find(section);
        if (si == ini.end()) return {};
        auto ki = si->second.find(key);
        return ki != si->second.end() ? ki->second : std::string{};
    };
    auto readKb = [&](const char* key, KeyBinding& kb) {
        auto raw = get("shortcuts", key);
        if (!raw.empty()) kb = KeyBinding::Deserialize(raw);
    };

    readKb("undo",        s.undo);
    readKb("redo",        s.redo);
    readKb("zoomIn",      s.zoomIn);
    readKb("zoomOut",     s.zoomOut);
    readKb("resetZoom",   s.resetZoom);
    readKb("fitToWindow", s.fitToWindow);
    readKb("toggleGrid",  s.toggleGrid);
    readKb("clearCanvas", s.clearCanvas);
    return s;
}

bool AppSettings::Save(const AppSettings& s, const std::string& path) {
    namespace fs = std::filesystem;
    if (const fs::path p(path); p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        if (ec) return false;
    }
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "; pelpaint settings — changes take effect after restart\n\n"
      << "[shortcuts]\n"
      << "undo="        << s.undo.Serialize()        << "\n"
      << "redo="        << s.redo.Serialize()         << "\n"
      << "zoomIn="      << s.zoomIn.Serialize()       << "\n"
      << "zoomOut="     << s.zoomOut.Serialize()      << "\n"
      << "resetZoom="   << s.resetZoom.Serialize()    << "\n"
      << "fitToWindow=" << s.fitToWindow.Serialize()  << "\n"
      << "toggleGrid="  << s.toggleGrid.Serialize()   << "\n"
      << "clearCanvas=" << s.clearCanvas.Serialize()  << "\n";
    return true;
}

} // namespace pelpaint
