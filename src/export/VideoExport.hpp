#pragma once

// export/VideoExport.hpp
// ---------------------------------------------------------------
// WASM / Emscripten video-export C bindings.
//
// These extern "C" functions are exposed to JavaScript via
// EMSCRIPTEN_KEEPALIVE + the -sEXPORTED_FUNCTIONS linker flag.
// JavaScript calls them as Module._video_export_xxx().
//
// Pipeline (JS side)
// ─────────────────
//   1. JS calls Module._video_export()            — entry point
//   2. C calls EM_ASM → Module.startVideoExport() — JS takes over
//   3. JS loops frames 0..N-1:
//        a. Module._malloc(w*h*4)                 — scratch buffer
//        b. Module._video_export_get_frame(i,ptr) — RGBA → MEMFS ptr
//        c. putImageData() on an OffscreenCanvas
//        d. MediaRecorder captures each frame
//   4. JS assembles WebM blob and triggers download
//
// The implementations live in VideoExport.cpp (EMSCRIPTEN only)
// and delegate to a globally registered PixelPaintView instance.
// ---------------------------------------------------------------

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cstdint>

extern "C" {

/// Total number of animation frames in the current timeline.
EMSCRIPTEN_KEEPALIVE int   video_export_frame_count();

/// Canvas width in pixels.
EMSCRIPTEN_KEEPALIVE int   video_export_frame_width();

/// Canvas height in pixels.
EMSCRIPTEN_KEEPALIVE int   video_export_frame_height();

/// Animation playback rate (frames per second).
EMSCRIPTEN_KEEPALIVE float video_export_fps();

/// Copy RGBA8 pixel data for frame `frameIdx` into `outRGBA`.
/// `outRGBA` must point to at least width * height * 4 bytes
/// (pre-allocated via Module._malloc on the JS side).
/// Returns 1 on success, 0 on failure.
EMSCRIPTEN_KEEPALIVE int   video_export_get_frame(int      frameIdx,
                                                   uint8_t* outRGBA,
                                                   int      bufLen);

/// Main entry point invoked from JavaScript.
/// Calls EM_ASM → Module.startVideoExport() to start the async capture.
EMSCRIPTEN_KEEPALIVE void  video_export();

} // extern "C"
#endif // __EMSCRIPTEN__
