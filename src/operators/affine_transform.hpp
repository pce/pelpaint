#pragma once

#include "FrameOperator.hpp"
#include "pp_rotate.hpp"
#include <cmath>
#include <algorithm>

namespace pelpaint::operators {

namespace detail {

// Bilinear sample from a w×h pixel buffer. Returns transparent for OOB.
inline Pixel bilinear(std::span<const Pixel> src, int w, int h,
                      float fx, float fy) noexcept {
    if (fx < 0.f || fy < 0.f || fx > static_cast<float>(w - 2) ||
        fy > static_cast<float>(h - 2))
        return {0, 0, 0, 0};

    const int   x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    const float tx = fx - x0,              ty = fy - y0;

    auto lp = [](uint8_t a, uint8_t b, float t) noexcept -> uint8_t {
        return static_cast<uint8_t>(
            static_cast<float>(a) + t * (static_cast<float>(b) - static_cast<float>(a)));
    };

    const Pixel& p00 = src[ y0      * w + x0    ];
    const Pixel& p10 = src[ y0      * w + x0 + 1];
    const Pixel& p01 = src[(y0 + 1) * w + x0    ];
    const Pixel& p11 = src[(y0 + 1) * w + x0 + 1];

    return {
        lp(lp(p00.r, p10.r, tx), lp(p01.r, p11.r, tx), ty),
        lp(lp(p00.g, p10.g, tx), lp(p01.g, p11.g, tx), ty),
        lp(lp(p00.b, p10.b, tx), lp(p01.b, p11.b, tx), ty),
        lp(lp(p00.a, p10.a, tx), lp(p01.a, p11.a, tx), ty),
    };
}

} // namespace detail

// ---------------------------------------------------------------------------
// affine_rotate  —  rotate by angleDeg degrees around the canvas centre.
//
//   PixelPerfect mode: snaps to nearest 90° increment (delegates to pp_rotate).
//   FreeDraw mode:     bilinear inverse-mapped rotation at any angle.
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn affine_rotate(float angleDeg) {
    return [angleDeg](std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        if (src.empty() || ctx.width <= 0 || ctx.height <= 0)
            return std::unexpected(Error::InvalidDims());

        if (ctx.mode == DrawMode::PixelPerfect) {
            const int snapped =
                ((static_cast<int>(std::round(angleDeg / 90.f)) % 4) + 4) % 4;
            if (snapped == 0)
                return std::vector<Pixel>(src.begin(), src.end());
            static constexpr RotateAmount kRots[] = {
                RotateAmount::R90, RotateAmount::R180, RotateAmount::R270};
            return pp_rotate(kRots[snapped - 1])(src, ctx);
        }

        // FreeDraw: inverse-map bilinear
        const int   w    = ctx.width,  h    = ctx.height;
        const float rad  = angleDeg * (3.14159265358979f / 180.f);
        const float cosA = std::cos(-rad), sinA = std::sin(-rad); // inverse map
        const float cx   = w * 0.5f - 0.5f, cy = h * 0.5f - 0.5f;

        std::vector<Pixel> out(static_cast<std::size_t>(w * h), Pixel{0, 0, 0, 0});
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float rx = x - cx, ry = y - cy;
                const float sx = cosA * rx - sinA * ry + cx;
                const float sy = sinA * rx + cosA * ry + cy;
                out[y * w + x] = detail::bilinear(src, w, h, sx, sy);
            }
        }
        return out;
    };
}

// ---------------------------------------------------------------------------
// affine_scale  —  scale around the canvas centre.
//
//   sy < 0: use sx for both axes (uniform scale).
//   PixelPerfect: nearest-neighbor, integer scale, output fitted to original dims.
//   FreeDraw:     bilinear inverse-map, output fitted to original dims.
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn affine_scale(float sx, float sy = -1.f) {
    return [sx, sy](std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        if (src.empty() || ctx.width <= 0 || ctx.height <= 0)
            return std::unexpected(Error::InvalidDims());

        const float scaleY = (sy < 0.f) ? sx : sy;
        const int   w = ctx.width, h = ctx.height;
        const float cx = w * 0.5f, cy = h * 0.5f;

        std::vector<Pixel> out(src.size(), Pixel{0, 0, 0, 0});

        if (ctx.mode == DrawMode::PixelPerfect) {
            const int isx = std::max(1, static_cast<int>(std::round(sx)));
            const int isy = std::max(1, static_cast<int>(std::round(scaleY)));
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const int ox = static_cast<int>(std::round((x - cx) / isx + cx));
                    const int oy = static_cast<int>(std::round((y - cy) / isy + cy));
                    if (ox >= 0 && ox < w && oy >= 0 && oy < h)
                        out[y * w + x] = src[oy * w + ox];
                }
            return out;
        }

        // FreeDraw: bilinear
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const float fx = (x - cx) / sx + cx;
                const float fy = (y - cy) / scaleY + cy;
                out[y * w + x] = detail::bilinear(src, w, h, fx, fy);
            }
        return out;
    };
}

// ---------------------------------------------------------------------------
// affine_shear  —  horizontal / vertical shear around canvas centre.
//
//   shearX: columns shift horizontally by shearX * (y - cy).
//   shearY: rows   shift vertically   by shearY * (x - cx).
// ---------------------------------------------------------------------------
[[nodiscard]] inline OpFn affine_shear(float shearX, float shearY = 0.f) {
    return [shearX, shearY](std::span<const Pixel> src, const OpCtx& ctx) -> OpResult {
        if (src.empty() || ctx.width <= 0 || ctx.height <= 0)
            return std::unexpected(Error::InvalidDims());

        const int   w = ctx.width, h = ctx.height;
        const float cx = w * 0.5f, cy = h * 0.5f;

        std::vector<Pixel> out(src.size(), Pixel{0, 0, 0, 0});

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float fx = static_cast<float>(x) - shearX * (y - cy);
                const float fy = static_cast<float>(y) - shearY * (x - cx);
                if (ctx.mode == DrawMode::PixelPerfect) {
                    const int sx = static_cast<int>(std::round(fx));
                    const int sy = static_cast<int>(std::round(fy));
                    if (sx >= 0 && sx < w && sy >= 0 && sy < h)
                        out[y * w + x] = src[sy * w + sx];
                } else {
                    out[y * w + x] = detail::bilinear(src, w, h, fx, fy);
                }
            }
        }
        return out;
    };
}

} // namespace pelpaint::operators
