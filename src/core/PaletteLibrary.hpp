/// @file PaletteLibrary.hpp
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "../ColorPalettes.hpp"

namespace pelpaint {

/// Mutable named-palette collection with JSON I/O.
///
/// Start with BuiltIn(); merge additional JSON files with LoadFile()/LoadJson().
/// Upsert() replaces an existing entry matched by name.
class PaletteLibrary {
public:
    /// Returns a library pre-populated with all built-in palettes.
    [[nodiscard]] static PaletteLibrary BuiltIn();

    /// Merge palettes from a JSON file.  Duplicate names replace existing entries.
    bool LoadFile(const std::string& path);

    /// Merge palettes from a JSON string.  Duplicate names replace existing entries.
    bool LoadJson(std::string_view json);

    [[nodiscard]] std::string ToJson() const;
    bool SaveFile(const std::string& path) const;

    [[nodiscard]] const std::vector<ColorPalette>& All()  const noexcept { return palettes_; }
    [[nodiscard]]       std::vector<ColorPalette>& All()        noexcept { return palettes_; }

    /// Returns nullptr when not found.
    [[nodiscard]] const ColorPalette* FindByName(std::string_view name) const noexcept;

    /// Returns -1 when not found.
    [[nodiscard]] int IndexOf(std::string_view name) const noexcept;

    void Upsert(ColorPalette palette);
    bool Remove(std::string_view name);

private:
    std::vector<ColorPalette> palettes_;
    bool ParseJson(std::string_view json);
};

} // namespace pelpaint
