#pragma once

#include <cstdint>
#include <vector>
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
    int zIndex = 0;
    float opacity = 1.0f;
    bool visible = true;
};

class SurfaceBackend {
public:
    virtual ~SurfaceBackend() = default;

    virtual bool Initialize(void* nativeWindow) = 0;
    virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

    virtual bool EnsureLayer(std::uint32_t layerId,
                             std::uint32_t width,
                             std::uint32_t height) = 0;

    virtual void UploadTile(std::uint32_t layerId,
                            std::uint32_t tx,
                            std::uint32_t ty,
                            const core::ImageView& view) = 0;

    virtual void RenderLayers(const std::vector<LayerInfo>& layers) = 0;
    virtual void Present() = 0;
};

// SDL3 GPU scaffold (no-op until wired)
class SdlGpuBackend final : public SurfaceBackend {
public:
    bool Initialize(void* /*nativeWindow*/) override { return false; }
    void Resize(std::uint32_t /*width*/, std::uint32_t /*height*/) override {}

    bool EnsureLayer(std::uint32_t /*layerId*/,
                     std::uint32_t /*width*/,
                     std::uint32_t /*height*/) override
    {
        return false;
    }

    void UploadTile(std::uint32_t /*layerId*/,
                    std::uint32_t /*tx*/,
                    std::uint32_t /*ty*/,
                    const core::ImageView& /*view*/) override
    {
    }

    void RenderLayers(const std::vector<LayerInfo>& /*layers*/) override {}
    void Present() override {}
};

} // namespace pelpaint::render
