/// @file PaletteRef.hpp
#pragma once

#include <span>
#include <vector>
#include "../ColorPalettes.hpp"

namespace pelpaint {

class PaletteLibrary;

/// How an operation's colour palette is determined.
enum class PaletteSource : uint8_t {
    None,    ///< Full-colour mode — no palette constraint.
    Named,   ///< A PaletteLibrary entry identified by index.
    Custom,  ///< User-defined colours stored inline in PaletteRef::colors.
    Auto,    ///< Unique colours extracted from the source image at call time.
};

/// Palette descriptor passed to every filter and apply operation.
///
/// Construct with the static helpers; call resolve() immediately before use.
struct PaletteRef {
    PaletteSource      source        = PaletteSource::None;
    int                namedIndex    = -1;   ///< Valid when source == Named.
    std::vector<Pixel> colors;               ///< Populated when source == Custom.
    int                maxAutoColors = 256;  ///< Extraction cap when source == Auto.

    /// Returns the concrete colour span for an operation.
    ///
    /// Named/Custom: zero-copy span into existing storage.
    /// Auto: extracts unique colours by frequency from @p imagePixels into
    ///       @p out_auto (up to maxAutoColors), then returns a span into it.
    ///       Returns an empty span when imagePixels is empty.
    /// None: empty span.
    ///
    /// @param out_auto  Caller-owned buffer — written only for Auto; ignored otherwise.
    [[nodiscard]] std::span<const Pixel> resolve(
        const PaletteLibrary&  library,
        std::span<const Pixel> imagePixels,
        std::vector<Pixel>&    out_auto) const;

    [[nodiscard]] static PaletteRef fromNamed(int index) noexcept;
    [[nodiscard]] static PaletteRef fromCustom(std::vector<Pixel> cols);
    [[nodiscard]] static PaletteRef fromAuto(int maxColors = 256) noexcept;
};

} // namespace pelpaint
