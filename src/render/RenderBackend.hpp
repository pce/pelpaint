#pragma once

#include <concepts>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include "../core/ImageSurface.hpp"

#if defined(__has_include)
    #if __has_include(<SDL3/SDL_gpu.h>)
        #define PELPAINT_HAS_SDL_GPU 1
        #include <SDL3/SDL_gpu.h>
    #else
        #define PELPAINT_HAS_SDL_GPU 0
    #endif
#else
    #define PELPAINT_HAS_SDL_GPU 0
#endif

namespace pelpaint::render {

struct LayerInfo {
    std::uint32_t layerId = 0;
    int           zIndex  = 0;
    float         opacity = 1.0f;
    bool          visible = true;
};

// Satisfied by any concrete surface backend; no inheritance required.
template<typename B>
concept SurfaceBackendConcept =
    std::movable<B> && std::destructible<B> &&
    requires(B& b, void* nativeWindow,
             std::uint32_t w, std::uint32_t h,
             std::uint32_t layerId, std::uint32_t tx, std::uint32_t ty,
             const core::ImageView& view,
             std::span<const LayerInfo> layers)
    {
        { b.Initialize(nativeWindow)        } -> std::convertible_to<bool>;
        { b.Resize(w, h)                    } -> std::same_as<void>;
        { b.EnsureLayer(layerId, w, h)      } -> std::convertible_to<bool>;
        { b.UploadTile(layerId, tx, ty, view) } -> std::same_as<void>;
        { b.RenderLayers(layers)            } -> std::same_as<void>;
        { b.Present()                       } -> std::same_as<void>;
    };

// C++23 deducing-this mixin — no CRTP template parameter needed.
struct SurfaceMixin {
    [[nodiscard]] bool CanUpload(this auto const& /*self*/,
                                 std::size_t /*pixelCount*/) noexcept {
        return true;
    }
};

// Owning type-erased handle. Dispatches via a per-type static Ops table
// (kOps<B>) — no vtable pointer stored in the concrete objects.
class SurfaceHandle {
public:
    SurfaceHandle() noexcept = default;

    ~SurfaceHandle() noexcept {
        if (data_) ops_->destroy(data_);
    }

    SurfaceHandle(SurfaceHandle&& o) noexcept
        : ops_(o.ops_), data_(std::exchange(o.data_, nullptr)) {}

    SurfaceHandle& operator=(SurfaceHandle&& o) noexcept {
        if (this != &o) {
            if (data_) ops_->destroy(data_);
            ops_  = o.ops_;
            data_ = std::exchange(o.data_, nullptr);
        }
        return *this;
    }

    SurfaceHandle(const SurfaceHandle&)            = delete;
    SurfaceHandle& operator=(const SurfaceHandle&) = delete;

    [[nodiscard]] bool valid()    const noexcept { return data_ != nullptr; }
    explicit operator bool()      const noexcept { return valid(); }

    template<SurfaceBackendConcept B>
    explicit SurfaceHandle(std::unique_ptr<B> impl) noexcept
        : ops_(&kOps<B>), data_(impl.release()) {}

    [[nodiscard]] bool Initialize(void* nativeWindow) {
        return ops_->init(data_, nativeWindow);
    }

    void Resize(std::uint32_t w, std::uint32_t h) {
        ops_->resize(data_, w, h);
    }

    [[nodiscard]] bool EnsureLayer(std::uint32_t layerId,
                                   std::uint32_t w,
                                   std::uint32_t h) {
        return ops_->ensureLayer(data_, layerId, w, h);
    }

    void UploadTile(std::uint32_t          layerId,
                    std::uint32_t          tx,
                    std::uint32_t          ty,
                    const core::ImageView& view) {
        ops_->uploadTile(data_, layerId, tx, ty, view);
    }

    void RenderLayers(std::span<const LayerInfo> layers) {
        ops_->renderLayers(data_, layers);
    }

    void Present() {
        ops_->present(data_);
    }

private:
    struct Ops {
        using InitFn         = bool (*)(void*, void*);
        using ResizeFn       = void (*)(void*, std::uint32_t, std::uint32_t);
        using EnsureLayerFn  = bool (*)(void*, std::uint32_t, std::uint32_t, std::uint32_t);
        using UploadTileFn   = void (*)(void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                        const core::ImageView&);
        using RenderLayersFn = void (*)(void*, std::span<const LayerInfo>);
        using PresentFn      = void (*)(void*);
        using DtorFn         = void (*)(void*) noexcept;

        InitFn         init;
        ResizeFn       resize;
        EnsureLayerFn  ensureLayer;
        UploadTileFn   uploadTile;
        RenderLayersFn renderLayers;
        PresentFn      present;
        DtorFn         destroy;
    };

    template<SurfaceBackendConcept B>
    static const Ops kOps;

    const Ops* ops_  = nullptr;
    void*      data_ = nullptr;
};

// One kOps<B> in static storage per concrete type.
// Captureless lambdas (+prefix → function pointer); B is a template param, not a capture.
template<SurfaceBackendConcept B>
inline const SurfaceHandle::Ops SurfaceHandle::kOps {
    .init         = +[](void* p, void* w) -> bool {
                        return static_cast<B*>(p)->Initialize(w); },

    .resize       = +[](void* p, std::uint32_t w, std::uint32_t h) -> void {
                        static_cast<B*>(p)->Resize(w, h); },

    .ensureLayer  = +[](void* p,
                         std::uint32_t id,
                         std::uint32_t w,
                         std::uint32_t h) -> bool {
                        return static_cast<B*>(p)->EnsureLayer(id, w, h); },

    .uploadTile   = +[](void* p,
                         std::uint32_t id,
                         std::uint32_t tx,
                         std::uint32_t ty,
                         const core::ImageView& view) -> void {
                        static_cast<B*>(p)->UploadTile(id, tx, ty, view); },

    .renderLayers = +[](void* p, std::span<const LayerInfo> layers) -> void {
                        static_cast<B*>(p)->RenderLayers(layers); },

    .present      = +[](void* p) -> void {
                        static_cast<B*>(p)->Present(); },

    .destroy      = +[](void* p) noexcept { delete static_cast<B*>(p); },
};

} // namespace pelpaint::render
