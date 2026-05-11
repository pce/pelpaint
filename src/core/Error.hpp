#pragma once

#include <expected>
#include <string_view>
#include <cstdint>

namespace pelpaint {

/// Error codes surfaced by tools, filters, exporters and generators.
enum class ErrorCode : std::uint8_t {
    None = 0,
    NullLayer,           ///< No active layer is available
    InvalidDimensions,   ///< Canvas width or height is zero / negative
    OutOfBounds,         ///< Pixel coordinate is outside the canvas bounds
    AllocationFailed,    ///< Memory allocation (std::bad_alloc or resize) failed
    EmptyPalette,        ///< A palette was required but is empty
    FileIOFailed,        ///< File open / write / read failed
    InvalidImageFormat,  ///< Unsupported channel count or corrupt image data
    InvalidGridSize,     ///< gridSize / blockSize must be > 0
    Cancelled,           ///< Operation was cancelled via stop_token
};

/// Lightweight, copyable, non-owning error value — zero heap allocation.
struct Error {
    ErrorCode        code    = ErrorCode::None;
    std::string_view message = {};

    // ---- factory helpers -----------------------------------------------

    [[nodiscard]] static constexpr Error NullLayer() noexcept {
        return {ErrorCode::NullLayer, "No active layer"};
    }
    [[nodiscard]] static constexpr Error InvalidDims() noexcept {
        return {ErrorCode::InvalidDimensions, "Canvas dimensions are zero"};
    }
    [[nodiscard]] static constexpr Error EmptyPalette() noexcept {
        return {ErrorCode::EmptyPalette, "Palette is empty"};
    }
    [[nodiscard]] static constexpr Error AllocFailed() noexcept {
        return {ErrorCode::AllocationFailed, "Memory allocation failed"};
    }
    [[nodiscard]] static constexpr Error FileIO(
        std::string_view msg = "File I/O failed") noexcept {
        return {ErrorCode::FileIOFailed, msg};
    }
    [[nodiscard]] static constexpr Error InvalidFormat() noexcept {
        return {ErrorCode::InvalidImageFormat, "Invalid image format"};
    }
    [[nodiscard]] static constexpr Error InvalidGridSz() noexcept {
        return {ErrorCode::InvalidGridSize, "Grid size must be > 0"};
    }
    [[nodiscard]] static constexpr Error Cancelled() noexcept {
        return {ErrorCode::Cancelled, "Operation cancelled"};
    }
};


// Convenience wrappers for constructing std::expected<T, Error>

/// Wrap a T in a value-containing expected<T, Error>.
template <class T>
[[nodiscard]] constexpr std::expected<T, Error> ok(T&& v) {
    return std::expected<T, Error>{std::forward<T>(v)};
}

/// Return a value-containing expected<void, Error>.
[[nodiscard]] inline constexpr std::expected<void, Error> ok() noexcept {
    return {};
}

} // namespace pelpaint
