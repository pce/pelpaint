// GpuBuffer<T> uses posix_memalign + newBufferWithBytesNoCopy so Metal never
// makes a host→device copy.  On unified memory host and GPU share physical
// memory; on non-unified (PCIe) MTLResourceStorageModeShared gives the GPU
// direct access to system RAM.  The deallocator block transfers ownership of
// the allocation to Metal's ARC — no manual ::free() needed.

#import "MetalComputeBackend.hpp"
#include "CpuComputeBackend.hpp"

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mdspan>
#include <numeric>
#include <ranges>
#include <span>

#include "../filters/Filters.hpp"

namespace pelpaint::render {

static NSString* const kShaderSrc = @R"MSL(
#include <metal_stdlib>
using namespace metal;

// Must match pelpaint::Pixel / PixelRGBA8 exactly (4-byte RGBA, no padding).
struct Pixel { uchar r, g, b, a; };

struct CompositeParams { float opacity; uint pad[3]; };

kernel void k_composite(
    device       Pixel*            dst    [[buffer(0)]],
    device const Pixel*            src    [[buffer(1)]],
    constant     CompositeParams&  p      [[buffer(2)]],
    uint gid [[thread_position_in_grid]])
{
    Pixel s = src[gid];
    Pixel d = dst[gid];

    float sa = (float(s.a) / 255.0f) * p.opacity;
    float da =  float(d.a) / 255.0f;
    float oa = fma(da, 1.0f - sa, sa);        // sa + da*(1-sa)

    if (oa <= 0.0f) { dst[gid] = {0u, 0u, 0u, 0u}; return; }

    float inv    = 1.0f / oa;
    float oam1sa = da * (1.0f - sa);

    dst[gid] = {
        uchar(clamp(fma(float(s.r), sa, float(d.r) * oam1sa) * inv, 0.0f, 255.0f)),
        uchar(clamp(fma(float(s.g), sa, float(d.g) * oam1sa) * inv, 0.0f, 255.0f)),
        uchar(clamp(fma(float(s.b), sa, float(d.b) * oam1sa) * inv, 0.0f, 255.0f)),
        uchar(clamp(oa * 255.0f, 0.0f, 255.0f))
    };
}

struct CyclePaletteParams { int palSize; int offset; float threshold; uint pad; };

kernel void k_cycle_palette(
    device       Pixel*                pixels  [[buffer(0)]],
    device const Pixel*                palette [[buffer(1)]],
    constant     CyclePaletteParams&   p       [[buffer(2)]],
    uint gid [[thread_position_in_grid]])
{
    Pixel px = pixels[gid];
    if (px.a == 0u) return;

    float best_d = p.threshold + 0.001f;
    int   best   = -1;

    for (int i = 0; i < p.palSize; ++i) {
        Pixel c = palette[i];
        float dr = float(px.r) - float(c.r);
        float dg = float(px.g) - float(c.g);
        float db = float(px.b) - float(c.b);
        float da = float(px.a) - float(c.a);
        float d  = sqrt(dr*dr + dg*dg + db*db + da*da);
        if (d < best_d) { best_d = d; best = i; }
    }
    if (best >= 0) {
        // Robust modulo (always positive):
        int idx = ((best + p.offset) % p.palSize + p.palSize) % p.palSize;
        pixels[gid] = palette[idx];
    }
}

kernel void k_grayscale(
    device Pixel* pixels [[buffer(0)]],
    uint gid [[thread_position_in_grid]])
{
    Pixel p = pixels[gid];
    uchar lum = uchar(0.299f * float(p.r) + 0.587f * float(p.g) + 0.114f * float(p.b));
    pixels[gid] = {lum, lum, lum, p.a};
}
)MSL";

template<typename T>
struct GpuBuffer {
    id<MTLBuffer> mtlBuf = nil;   // ARC strong ref; deallocator owns the host allocation
    T*            host   = nullptr;
    std::size_t   count  = 0;

    GpuBuffer() = default;
    GpuBuffer(id<MTLBuffer> b, T* h, std::size_t n) : mtlBuf(b), host(h), count(n) {}

    GpuBuffer(GpuBuffer&&)            = default;
    GpuBuffer& operator=(GpuBuffer&&) = default;
    GpuBuffer(const GpuBuffer&)       = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    [[nodiscard]] bool        ok()       const noexcept { return mtlBuf != nil; }
    [[nodiscard]] std::size_t byteSize() const noexcept { return count * sizeof(T); }

    [[nodiscard]] std::span<T>       span()       noexcept { return {host, count}; }
    [[nodiscard]] std::span<const T> span() const noexcept { return {host, count}; }

    [[nodiscard]] auto mdspan2d(std::size_t rows, std::size_t cols) noexcept {
        return std::mdspan{host,
            std::extents<std::size_t,
                         std::dynamic_extent,
                         std::dynamic_extent>{rows, cols}};
    }
};

struct MetalComputeBackend::Impl {
    id<MTLDevice>               device       = nil;
    id<MTLCommandQueue>         queue        = nil;
    id<MTLComputePipelineState> psoComposite = nil;
    id<MTLComputePipelineState> psoCycle     = nil;
    id<MTLComputePipelineState> psoGrayscale = nil;

    bool hasUnifiedMemory = false;
    bool ok               = false;

    void Init(void* rawDevice)
    {
        if (!rawDevice) return;
        device = (__bridge id<MTLDevice>)rawDevice;
        if (!device) return;

        hasUnifiedMemory = device.hasUnifiedMemory;
        queue = [device newCommandQueue];
        if (!queue) return;

        NSError* compileErr = nil;
        id<MTLLibrary> lib = [device newLibraryWithSource:kShaderSrc
                                                   options:nil
                                                     error:&compileErr];
        if (!lib) {
            NSLog(@"[MetalComputeBackend] Shader compile error: %@", compileErr);
            return;
        }

        auto makePSO = [&](NSString* name) -> id<MTLComputePipelineState> {
            id<MTLFunction> fn = [lib newFunctionWithName:name];
            if (!fn) {
                NSLog(@"[MetalComputeBackend] Function '%@' not found", name);
                return nil;
            }
            NSError* psoErr = nil;
            auto pso = [device newComputePipelineStateWithFunction:fn error:&psoErr];
            if (!pso)
                NSLog(@"[MetalComputeBackend] PSO '%@' error: %@", name, psoErr);
            return pso;
        };

        psoComposite  = makePSO(@"k_composite");
        psoCycle      = makePSO(@"k_cycle_palette");
        psoGrayscale  = makePSO(@"k_grayscale");

        ok = psoComposite && psoCycle && psoGrayscale;
        if (ok)
            NSLog(@"[MetalComputeBackend] Initialized: %@ (unified=%d)",
                  device.name, (int)hasUnifiedMemory);
    }

    template<typename T>
    [[nodiscard]] std::expected<GpuBuffer<T>, Error>
    allocBuffer(std::size_t count) const
    {
        if (count == 0)
            return std::unexpected(Error::AllocFailed());

        const std::size_t bytes = count * sizeof(T);
        const std::size_t pages = (bytes + 4095u) & ~std::size_t{4095u};

        void* mem = nullptr;
        if (::posix_memalign(&mem, 4096u, pages) != 0)
            return std::unexpected(Error::AllocFailed());

        std::memset(mem, 0, pages);

        id<MTLBuffer> buf =
            [device newBufferWithBytesNoCopy:mem
                                      length:pages
                                     options:MTLResourceStorageModeShared
                                 deallocator:^(void* ptr, NSUInteger) { ::free(ptr); }];
        if (!buf) {
            ::free(mem);
            return std::unexpected(Error::AllocFailed());
        }

        return GpuBuffer<T>{ buf, static_cast<T*>(mem), count };
    }

    void dispatchSync(id<MTLComputePipelineState>    pso,
                      std::span<id<MTLBuffer> const> buffers,
                      const void*                    inlineParams = nullptr,
                      std::size_t                    inlineLen    = 0,
                      std::size_t                    threadCount  = 0) const
    {
        id<MTLCommandBuffer>         cmd = [queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:pso];
        for (NSUInteger i = 0; i < static_cast<NSUInteger>(buffers.size()); ++i)
            [enc setBuffer:buffers[i] offset:0 atIndex:i];
        if (inlineParams && inlineLen > 0)
            [enc setBytes:inlineParams
                   length:inlineLen
                  atIndex:static_cast<NSUInteger>(buffers.size())];
        const NSUInteger tgs =
            std::min<NSUInteger>(pso.maxTotalThreadsPerThreadgroup, 256u);
        [enc dispatchThreads:MTLSizeMake(threadCount, 1, 1)
         threadsPerThreadgroup:MTLSizeMake(tgs, 1, 1)];
        [enc endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];
    }

    static void EncodeBlitCopy(id<MTLCommandBuffer> cmd,
                                id<MTLBuffer>        src,
                                id<MTLBuffer>        dst,
                                std::size_t          bytes)
    {
        id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
        [blit copyFromBuffer:src sourceOffset:0
                    toBuffer:dst destinationOffset:0
                        size:bytes];
        [blit endEncoding];
    }

    // Encodes a compute dispatch into an existing command buffer (no commit).
    void encodeDispatch(id<MTLCommandBuffer>           cmd,
                        id<MTLComputePipelineState>    pso,
                        std::span<id<MTLBuffer> const> buffers,
                        const void*                    inlineParams,
                        std::size_t                    inlineLen,
                        std::size_t                    threadCount) const
    {
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:pso];
        for (NSUInteger i = 0; i < static_cast<NSUInteger>(buffers.size()); ++i)
            [enc setBuffer:buffers[i] offset:0 atIndex:i];
        if (inlineParams && inlineLen)
            [enc setBytes:inlineParams
                   length:inlineLen
                  atIndex:static_cast<NSUInteger>(buffers.size())];
        const NSUInteger tgs =
            std::min<NSUInteger>(pso.maxTotalThreadsPerThreadgroup, 256u);
        [enc dispatchThreads:MTLSizeMake(threadCount, 1, 1)
         threadsPerThreadgroup:MTLSizeMake(tgs, 1, 1)];
        [enc endEncoding];
    }
};

MetalComputeBackend::MetalComputeBackend(void* metalDevice)
    : impl_(std::make_unique<Impl>())
{
    impl_->Init(metalDevice);
}

MetalComputeBackend::~MetalComputeBackend()                                      = default;
MetalComputeBackend::MetalComputeBackend(MetalComputeBackend&&) noexcept            = default;
MetalComputeBackend& MetalComputeBackend::operator=(MetalComputeBackend&&) noexcept = default;

ComputeCapabilities MetalComputeBackend::Capabilities() const noexcept
{
    if (!impl_->ok)
        return {BackendCapability::None, false, 0, "Metal (init failed — CPU fallback)"};

    const bool unified = impl_->hasUnifiedMemory;
    const auto flags   = BackendCapability::GpuComposite
                       | BackendCapability::GpuFilters
                       | BackendCapability::GpuCycling
                       | (unified ? BackendCapability::UnifiedMemory
                                  : BackendCapability::None);

    // Unified memory has negligible dispatch overhead so the threshold is lower.
    const int threshold = unified ? 4096 : 16384;

    NSString* desc = [NSString stringWithFormat:@"Metal — %@ (%@)",
                      impl_->device.name,
                      unified ? @"unified memory, zero-copy" : @"shared memory"];

    return {flags, true, threshold, desc.UTF8String};
}

std::expected<effects::PaletteCycleResult, Error>
MetalComputeBackend::BakePaletteCycleFrames(
    core::AnimationTimeline&           timeline,
    const Canvas&                      canvas,
    const effects::PaletteCycleConfig& cfg)
{
    if (!impl_->ok)
        return std::unexpected(Error{ErrorCode::NullLayer, "Metal backend unavailable"});

    const auto& layers = canvas.Layers();
    if (cfg.cyclingLayerIdx < 0 ||
        cfg.cyclingLayerIdx >= static_cast<int>(layers.size()))
        return std::unexpected(Error{ErrorCode::OutOfBounds,
                                     "cyclingLayerIdx out of range"});

    if (cfg.cycleColors.empty())
        return std::unexpected(Error::EmptyPalette());

    const int         N       = static_cast<int>(cfg.cycleColors.size());
    const int         W       = canvas.Width();
    const int         H       = canvas.Height();
    const std::size_t nPx     = static_cast<std::size_t>(W) * H;
    const std::size_t byteLen = nPx * sizeof(pelpaint::Pixel);

    const auto& cycleLayer = layers[static_cast<std::size_t>(cfg.cyclingLayerIdx)];
    if (cycleLayer.pixelData.size() != nPx)
        return std::unexpected(Error{ErrorCode::InvalidDimensions,
                                     "Cycling layer size != canvas size"});

    auto sortedIdx =
        std::views::iota(0, static_cast<int>(layers.size()))
        | std::ranges::to<std::vector<int>>();
    std::ranges::stable_sort(sortedIdx, std::less<>{},
        [&](int i) { return layers[static_cast<std::size_t>(i)].zIndex; });

    auto backdropBufOrErr = impl_->allocBuffer<pelpaint::Pixel>(nPx);
    if (!backdropBufOrErr) return std::unexpected(backdropBufOrErr.error());
    auto backdropBuf = std::move(*backdropBufOrErr);

    for (int li : sortedIdx) {
        if (li == cfg.cyclingLayerIdx) continue;
        const auto& lay = layers[static_cast<std::size_t>(li)];
        if (!lay.visible || lay.pixelData.size() != nPx) continue;

        auto layBufOrErr = impl_->allocBuffer<pelpaint::Pixel>(nPx);
        if (!layBufOrErr) continue;
        auto layBuf = std::move(*layBufOrErr);
        std::ranges::copy(lay.pixelData, layBuf.span().begin());

        struct { float opacity; std::uint32_t pad[3]; } cp{lay.opacity, {0, 0, 0}};
        impl_->dispatchSync(impl_->psoComposite,
            std::array<id<MTLBuffer>, 2>{backdropBuf.mtlBuf, layBuf.mtlBuf},
            &cp, sizeof(cp), nPx);
    }

    auto cyclePxResult = cfg.quantizeFirst
        ? filters::QuantiseToPalette(
              cycleLayer.pixelData,
              std::span<const pelpaint::Pixel>{cfg.cycleColors})
          .transform([](std::vector<pelpaint::Pixel> px) { return std::move(px); })
        : std::expected<std::vector<pelpaint::Pixel>, Error>{ cycleLayer.pixelData };

    if (!cyclePxResult) return std::unexpected(cyclePxResult.error());
    const auto& cyclePx      = *cyclePxResult;
    const float cycleOpacity = cycleLayer.opacity;

    auto palBufOrErr = impl_->allocBuffer<pelpaint::Pixel>(cfg.cycleColors.size());
    if (!palBufOrErr) return std::unexpected(palBufOrErr.error());
    auto palBuf = std::move(*palBufOrErr);
    std::ranges::copy(cfg.cycleColors, palBuf.span().begin());

    auto scratchBufOrErr = impl_->allocBuffer<pelpaint::Pixel>(nPx);
    auto frameBufOrErr   = impl_->allocBuffer<pelpaint::Pixel>(nPx);
    if (!scratchBufOrErr) return std::unexpected(scratchBufOrErr.error());
    if (!frameBufOrErr)   return std::unexpected(frameBufOrErr.error());
    auto scratchBuf = std::move(*scratchBufOrErr);
    auto frameBuf   = std::move(*frameBufOrErr);

    effects::PaletteCycleResult result;
    result.cycleLength = N;
    result.frameIndices.reserve(static_cast<std::size_t>(N));

    for (int step = 0; step < N; ++step) {
        std::ranges::copy(cyclePx, scratchBuf.span().begin());

        id<MTLCommandBuffer> cmd = [impl_->queue commandBuffer];

        if (step > 0) {
            struct CyclePaletteParams {
                int palSize; int offset; float threshold; std::uint32_t pad;
            } cycp{N, step, cfg.matchThreshold, 0u};
            impl_->encodeDispatch(cmd, impl_->psoCycle,
                std::array<id<MTLBuffer>, 2>{scratchBuf.mtlBuf, palBuf.mtlBuf},
                &cycp, sizeof(cycp), nPx);
        }

        Impl::EncodeBlitCopy(cmd, backdropBuf.mtlBuf, frameBuf.mtlBuf, byteLen);

        struct { float opacity; std::uint32_t pad[3]; } cp{cycleOpacity, {0, 0, 0}};
        impl_->encodeDispatch(cmd, impl_->psoComposite,
            std::array<id<MTLBuffer>, 2>{frameBuf.mtlBuf, scratchBuf.mtlBuf},
            &cp, sizeof(cp), nPx);

        [cmd commit];
        [cmd waitUntilCompleted];

        const int frameIdx = timeline.AddFrame();
        auto& frame = timeline.Frame(frameIdx);
        frame.label = "Cycle " + std::to_string(step) + "/" + std::to_string(N - 1);
        if (cfg.frameDelay > 0.f) frame.delay = cfg.frameDelay;

        const std::span<const core::PixelRGBA8> flatSpan{
            reinterpret_cast<const core::PixelRGBA8*>(frameBuf.host), nPx};
        frame.surface.WriteFlat(flatSpan);

        frame.layerStates.reserve(layers.size());
        for (int li : sortedIdx)
            frame.layerStates.push_back({
                li,
                li == cfg.cyclingLayerIdx
                    ? true
                    : layers[static_cast<std::size_t>(li)].visible,
                li == cfg.cyclingLayerIdx
                    ? cycleLayer.opacity
                    : layers[static_cast<std::size_t>(li)].opacity});

        result.frameIndices.push_back(frameIdx);
    }

    return result;
}

std::expected<std::vector<pelpaint::Pixel>, Error>
MetalComputeBackend::ApplyCyclePalette(
    std::span<const pelpaint::Pixel> src,
    int w, int h,
    std::span<const pelpaint::Pixel> cycleColors,
    int                              offset,
    float                            matchThreshold)
{
    if (!impl_->ok || src.empty())
        return std::vector<pelpaint::Pixel>(src.begin(), src.end());

    const std::size_t nPx = static_cast<std::size_t>(w) * h;

    auto pixBufOrErr = impl_->allocBuffer<pelpaint::Pixel>(nPx);
    auto palBufOrErr = impl_->allocBuffer<pelpaint::Pixel>(cycleColors.size());
    if (!pixBufOrErr) return std::unexpected(pixBufOrErr.error());
    if (!palBufOrErr) return std::unexpected(palBufOrErr.error());
    auto pixBuf = std::move(*pixBufOrErr);
    auto palBuf = std::move(*palBufOrErr);

    std::ranges::copy(src,         pixBuf.span().begin());
    std::ranges::copy(cycleColors, palBuf.span().begin());

    struct CyclePaletteParams {
        int palSize; int offset; float threshold; std::uint32_t pad;
    } p{static_cast<int>(cycleColors.size()), offset, matchThreshold, 0u};

    impl_->dispatchSync(impl_->psoCycle,
        std::array<id<MTLBuffer>, 2>{pixBuf.mtlBuf, palBuf.mtlBuf},
        &p, sizeof(p), nPx);

    return std::vector<pelpaint::Pixel>(pixBuf.span().begin(), pixBuf.span().end());
}

std::expected<std::vector<pelpaint::Pixel>, Error>
MetalComputeBackend::ApplyGrayscale(std::span<const pelpaint::Pixel> src)
{
    if (!impl_->ok || src.empty())
        return std::vector<pelpaint::Pixel>(src.begin(), src.end());

    auto bufOrErr = impl_->allocBuffer<pelpaint::Pixel>(src.size());
    if (!bufOrErr) return std::unexpected(bufOrErr.error());
    auto buf = std::move(*bufOrErr);

    std::ranges::copy(src, buf.span().begin());
    impl_->dispatchSync(impl_->psoGrayscale,
        std::array{buf.mtlBuf}, nullptr, 0, src.size());

    return std::vector<pelpaint::Pixel>(buf.span().begin(), buf.span().end());
}

Backend Backend::Create(void* metalDevice)
{
    if (metalDevice) {
        auto metal = std::make_unique<MetalComputeBackend>(metalDevice);
        if (metal->Capabilities().hasGpu)
            return Backend{ std::move(metal) };
        // Metal init failed (shader compile error, etc.) — fall through.
    }
    return Backend{ std::make_unique<CpuComputeBackend>() };
}

} // namespace pelpaint::render
