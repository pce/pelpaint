#include "ImageSlicer.hpp"
#include <cmath>
#include <algorithm>

namespace pelpaint::slicer {

std::vector<Slice> DepthThresholdGenerator::Generate(
    std::span<const uint8_t> rgba,
    std::span<const uint8_t> depth,
    int w, int h,
    const GeneratorOptions& opts)
{
    std::vector<Slice> slices;
    if (opts.numSlices <= 0 || rgba.empty() || depth.empty()) return slices;

    int step = 255 / opts.numSlices;
    
    for (int i = 0; i < opts.numSlices; ++i) {
        int lower = i * step;
        int upper = (i + 1) * step;
        if (i == opts.numSlices - 1) upper = 255;
        
        Slice slice;
        slice.id = i;
        slice.label = "Layer_" + std::to_string(i);
        slice.width = w;
        slice.height = h;
        slice.pixels.resize(static_cast<std::size_t>(w * h * 4), 0);
        
        slice.depth.lo = lower / 255.f;
        slice.depth.hi = upper / 255.f;
        slice.parallaxFactor = 1.0f - slice.depth.lo;
        slice.fill.mode = opts.fillMode;
        
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int idx = y * w + x;
                uint8_t d = depth[idx];
                
                if (opts.invertDepth) {
                    d = 255 - d;
                }
                
                if (d >= lower && d <= upper) {
                    slice.pixels[idx * 4 + 0] = rgba[idx * 4 + 0];
                    slice.pixels[idx * 4 + 1] = rgba[idx * 4 + 1];
                    slice.pixels[idx * 4 + 2] = rgba[idx * 4 + 2];
                    slice.pixels[idx * 4 + 3] = d; // alpha mask
                }
            }
        }
        slices.push_back(std::move(slice));
    }
    
    return slices;
}



void FillHolesModifier::Apply(Slice& slice, float /*dt*/, const SceneContext& /*ctx*/) {
    if (slice.fill.mode == FillMode::None) return;
    
    int w = slice.width;
    int h = slice.height;
    
    // Simple edge stretch as a placeholder for clone/patch
    // This is priority 1, so let's do a fast stretch
    std::vector<uint8_t> newPixels = slice.pixels;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            if (slice.pixels[idx * 4 + 3] == 0) { // transparent
                // Find nearest opaque pixel
                int searchRadius = 4;
                bool found = false;
                for (int r = 1; r <= searchRadius && !found; ++r) {
                    for (int dy = -r; dy <= r && !found; ++dy) {
                        for (int dx = -r; dx <= r && !found; ++dx) {
                            if (dx == -r || dx == r || dy == -r || dy == r) {
                                int nx = x + dx;
                                int ny = y + dy;
                                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                                    int nidx = ny * w + nx;
                                    if (slice.pixels[nidx * 4 + 3] > 0) {
                                        newPixels[idx * 4 + 0] = slice.pixels[nidx * 4 + 0];
                                        newPixels[idx * 4 + 1] = slice.pixels[nidx * 4 + 1];
                                        newPixels[idx * 4 + 2] = slice.pixels[nidx * 4 + 2];
                                        newPixels[idx * 4 + 3] = slice.pixels[nidx * 4 + 3];
                                        found = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    slice.pixels = std::move(newPixels);
}

void FeatherEdgeModifier::Apply(Slice& slice, float /*dt*/, const SceneContext& /*ctx*/) {
    // Feather edge placeholder
}

void ParallaxMotionModifier::Apply(Slice& slice, float /*dt*/, const SceneContext& ctx) {
    slice.offset.x += ctx.cameraDelta.x * slice.parallaxFactor;
    slice.offset.y += ctx.cameraDelta.y * slice.parallaxFactor;
}

void ProceduralAnimModifier::Apply(Slice& slice, float dt, const SceneContext& ctx) {
    switch (slice.animPreset) {
        case AnimPreset::WindSway:
            slice.offset.x += std::sin(ctx.time * slice.motion.driftFrequency + slice.id * 0.7f) * slice.motion.driftAmplitude * dt;
            break;
        case AnimPreset::Breathe:
            slice.scale = 1.0f + std::sin(ctx.time * 0.8f) * 0.01f;
            break;
        case AnimPreset::CameraShake:
            slice.offset.x += ctx.shakeVector.x * slice.parallaxFactor;
            slice.offset.y += ctx.shakeVector.y * slice.parallaxFactor;
            break;
        default:
            break;
    }
}

} // namespace pelpaint::slicer
