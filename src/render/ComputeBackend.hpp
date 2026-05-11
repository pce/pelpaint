#pragma once

// Platform strategy:
//   macOS/iOS Apple Silicon  MetalComputeBackend  Unified — always zero-copy
//   macOS/iOS A9–A14         MetalComputeBackend  Shared  — coherent
//   macOS Intel+AMD          MetalComputeBackend  Shared  — PCIe coherent
//   Linux / Windows          CpuComputeBackend    n/a

#include <concepts>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../core/Error.hpp"
#include "../ColorPalettes.hpp"
#include "../effects/PaletteCycler.hpp"
#include "../core/AnimationTimeline.hpp"
#include "../core/Canvas.hpp"

namespace pelpaint::render {

enum class BackendCapability : std::uint32_t {
    None          = 0,
    GpuComposite  = 1u << 0,
    GpuFilters    = 1u << 1,
    GpuCycling    = 1u << 2,
    UnifiedMemory = 1u << 3,
    AsyncCompute  = 1u << 4,
};

[[nodiscard]] constexpr BackendCapability
operator|(BackendCapability a, BackendCapability b) noexcept {
    return static_cast<BackendCapability>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr bool
Has(BackendCapability flags, BackendCapability bit) noexcept {
    return (static_cast<std::uint32_t>(flags) &
            static_cast<std::uint32_t>(bit)) != 0;
}

struct ComputeCapabilities {
    BackendCapability flags        = BackendCapability::None;
    bool              hasGpu       = false;
    int               gpuMinPixels = 0;
    std::string       name;
};

// Satisfied by any concrete backend; no inheritance required.
template<typename B>
concept BackendConcept =
    std::movable<B> && std::destructible<B> &&
    requires(B& b, const B& cb,
             core::AnimationTimeline& tl, const Canvas& canvas,
             const effects::PaletteCycleConfig& cfg,
             std::span<const Pixel> src, std::span<const Pixel> pal,
             int w, int h, int offset, float threshold)
    {
        { cb.Capabilities() } noexcept -> std::convertible_to<ComputeCapabilities>;
        { b.BakePaletteCycleFrames(tl, canvas, cfg) }
            -> std::same_as<std::expected<effects::PaletteCycleResult, Error>>;
        { b.ApplyCyclePalette(src, w, h, pal, offset, threshold) }
            -> std::same_as<std::expected<std::vector<Pixel>, Error>>;
        { b.ApplyGrayscale(src) }
            -> std::same_as<std::expected<std::vector<Pixel>, Error>>;
    };

// C++23 deducing-this mixin — no CRTP template parameter needed.
struct BackendMixin {
    [[nodiscard]] bool PreferGpu(this auto const& self,
                                 std::size_t pixelCount) noexcept {
        const auto c = self.Capabilities();
        return c.hasGpu && static_cast<int>(pixelCount) >= c.gpuMinPixels;
    }
};

// Owning type-erased handle. Dispatches via a per-type static Ops table
// (kOps<B>) — no vtable pointer stored in the concrete objects.
class Backend {
public:
    Backend() noexcept = default;

    ~Backend() noexcept {
        if (data_) ops_->destroy(data_);
    }

    Backend(Backend&& o) noexcept
        : ops_(o.ops_), data_(std::exchange(o.data_, nullptr)) {}

    Backend& operator=(Backend&& o) noexcept {
        if (this != &o) {
            if (data_) ops_->destroy(data_);
            ops_  = o.ops_;
            data_ = std::exchange(o.data_, nullptr);
        }
        return *this;
    }

    Backend(const Backend&)            = delete;
    Backend& operator=(const Backend&) = delete;

    [[nodiscard]] bool valid()   const noexcept { return data_ != nullptr; }
    explicit operator bool()     const noexcept { return valid(); }

    template<BackendConcept B>
    explicit Backend(std::unique_ptr<B> impl) noexcept
        : ops_(&kOps<B>), data_(impl.release()) {}

    [[nodiscard]] ComputeCapabilities
    Capabilities() const noexcept { return ops_->caps(data_); }

    [[nodiscard]] bool
    PreferGpu(std::size_t pixelCount) const noexcept {
        const auto c = Capabilities();
        return c.hasGpu && static_cast<int>(pixelCount) >= c.gpuMinPixels;
    }

    [[nodiscard]] std::expected<effects::PaletteCycleResult, Error>
    BakePaletteCycleFrames(core::AnimationTimeline&           tl,
                            const Canvas&                      canvas,
                            const effects::PaletteCycleConfig& cfg,
                            effects::BakeControl               ctl = {}) {
        return ops_->bake(data_, tl, canvas, cfg, std::move(ctl));
    }

    [[nodiscard]] std::expected<std::vector<Pixel>, Error>
    ApplyCyclePalette(std::span<const Pixel> src,
                      int w, int h,
                      std::span<const Pixel> pal,
                      int offset, float threshold) {
        return ops_->cycle(data_, src, w, h, pal, offset, threshold);
    }

    [[nodiscard]] std::expected<std::vector<Pixel>, Error>
    ApplyGrayscale(std::span<const Pixel> src) {
        return ops_->gray(data_, src);
    }

    /// Returns the best available backend. metalDevice: id<MTLDevice> as void*;
    /// nullptr falls through to CpuComputeBackend.
    [[nodiscard]] static Backend Create(void* metalDevice = nullptr);

private:
    struct Ops {
        using CapsFn  = ComputeCapabilities (*)(const void*) noexcept;
        using BakeFn  = std::expected<effects::PaletteCycleResult, Error>
                        (*)(void*, core::AnimationTimeline&,
                            const Canvas&,
                            const effects::PaletteCycleConfig&,
                            effects::BakeControl);
        using CycleFn = std::expected<std::vector<Pixel>, Error>
                        (*)(void*, std::span<const Pixel>,
                            int, int,
                            std::span<const Pixel>, int, float);
        using GrayFn  = std::expected<std::vector<Pixel>, Error>
                        (*)(void*, std::span<const Pixel>);
        using DtorFn  = void (*)(void*) noexcept;

        CapsFn  caps;
        BakeFn  bake;
        CycleFn cycle;
        GrayFn  gray;
        DtorFn  destroy;
    };

    template<BackendConcept B>
    static const Ops kOps;

    const Ops* ops_  = nullptr;
    void*      data_ = nullptr;
};

// One kOps<B> in static storage per concrete type.
// Captureless lambdas (+prefix → function pointer); B is a template param, not a capture.
template<BackendConcept B>
inline const Backend::Ops Backend::kOps {
    .caps    = +[](const void* p) noexcept -> ComputeCapabilities {
                   return static_cast<const B*>(p)->Capabilities(); },

    .bake    = +[](void* p,
                   core::AnimationTimeline& tl,
                   const Canvas& c,
                   const effects::PaletteCycleConfig& cfg,
                   effects::BakeControl ctl)
                   -> std::expected<effects::PaletteCycleResult, Error> {
                   return static_cast<B*>(p)->BakePaletteCycleFrames(
                       tl, c, cfg, std::move(ctl)); },

    .cycle   = +[](void* p,
                   std::span<const Pixel> src, int w, int h,
                   std::span<const Pixel> pal, int off, float thr)
                   -> std::expected<std::vector<Pixel>, Error> {
                   return static_cast<B*>(p)->ApplyCyclePalette(
                       src, w, h, pal, off, thr); },

    .gray    = +[](void* p, std::span<const Pixel> src)
                   -> std::expected<std::vector<Pixel>, Error> {
                   return static_cast<B*>(p)->ApplyGrayscale(src); },

    .destroy = +[](void* p) noexcept { delete static_cast<B*>(p); },
};

} // namespace pelpaint::render
