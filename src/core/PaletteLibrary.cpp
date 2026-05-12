/// @file PaletteLibrary.cpp
/// @brief PaletteLibrary implementation — JSON I/O and collection management.
#include "PaletteLibrary.hpp"
#include "../ColorPalettes.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <ranges>
#include <nlohmann/json.hpp>

namespace pelpaint {

namespace {

/// Parse a CSS-style hex colour string (#RGB, #RRGGBB, or #RRGGBBAA).
/// Missing alpha defaults to 255.  Returns a zeroed Pixel on malformed input.
Pixel pixel_from_hex(std::string_view h) noexcept
{
    if (!h.empty() && h[0] == '#') h.remove_prefix(1);

    // Expand shorthand #RGB / #RGBA → #RRGGBB / #RRGGBBAA
    std::string expanded;
    if (h.size() == 3 || h.size() == 4) {
        expanded.reserve(h.size() * 2);
        for (char c : h) { expanded += c; expanded += c; }
        h = expanded;
    }

    if (h.size() != 6 && h.size() != 8) return {};

    auto nib = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return uint8_t(c - '0');
        if (c >= 'a' && c <= 'f') return uint8_t(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return uint8_t(c - 'A' + 10);
        return 0;
    };
    auto byte = [&](std::size_t i) {
        return uint8_t((nib(h[i]) << 4) | nib(h[i + 1]));
    };

    const uint8_t a = (h.size() == 8) ? byte(6) : 255u;
    return { byte(0), byte(2), byte(4), a };
}

/// Serialise a Pixel to a lowercase hex string.
/// Alpha is omitted when it equals 255.
std::string hex_from_pixel(const Pixel& p)
{
    char buf[10];
    if (p.a == 255)
        std::snprintf(buf, sizeof buf, "#%02x%02x%02x",       p.r, p.g, p.b);
    else
        std::snprintf(buf, sizeof buf, "#%02x%02x%02x%02x",   p.r, p.g, p.b, p.a);
    return buf;
}

} // namespace

PaletteLibrary PaletteLibrary::BuiltIn()
{
    PaletteLibrary lib;
    lib.palettes_ = palettes::GetAllPalettes();
    return lib;
}

bool PaletteLibrary::LoadFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f) return false;
    const std::string json{ std::istreambuf_iterator<char>(f),
                             std::istreambuf_iterator<char>() };
    return ParseJson(json);
}

bool PaletteLibrary::LoadJson(std::string_view json)
{
    return ParseJson(json);
}

bool PaletteLibrary::ParseJson(std::string_view json)
{
    try {
        const auto j = nlohmann::json::parse(json);
        if (!j.contains("palettes")) return false;

        for (const auto& entry : j["palettes"]) {
            ColorPalette pal(
                entry.value("name",        std::string("Unnamed")),
                {},
                entry.value("description", std::string(""))
            );
            for (const auto& c : entry.at("colors"))
                pal.colors.push_back(pixel_from_hex(c.get<std::string>()));
            Upsert(std::move(pal));
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string PaletteLibrary::ToJson() const
{
    nlohmann::json root;
    auto& arr = root["palettes"] = nlohmann::json::array();

    for (const auto& pal : palettes_) {
        nlohmann::json p;
        p["name"]        = pal.name;
        p["description"] = pal.description;
        auto& cols = p["colors"] = nlohmann::json::array();
        for (const auto& c : pal.colors)
            cols.push_back(hex_from_pixel(c));
        arr.push_back(std::move(p));
    }
    return root.dump(2);
}

bool PaletteLibrary::SaveFile(const std::string& path) const
{
    std::ofstream f(path);
    if (!f) return false;
    f << ToJson();
    return f.good();
}

const ColorPalette* PaletteLibrary::FindByName(std::string_view name) const noexcept
{
    for (const auto& p : palettes_)
        if (p.name == name) return &p;
    return nullptr;
}

int PaletteLibrary::IndexOf(std::string_view name) const noexcept
{
    for (int i = 0; i < static_cast<int>(palettes_.size()); ++i)
        if (palettes_[i].name == name) return i;
    return -1;
}

void PaletteLibrary::Upsert(ColorPalette palette)
{
    for (auto& p : palettes_) {
        if (p.name == palette.name) { p = std::move(palette); return; }
    }
    palettes_.push_back(std::move(palette));
}

bool PaletteLibrary::Remove(std::string_view name)
{
    auto it = std::ranges::find_if(palettes_,
                                   [&](const auto& p) { return p.name == name; });
    if (it == palettes_.end()) return false;
    palettes_.erase(it);
    return true;
}

} // namespace pelpaint
