// export/VideoExport.cpp
// ---------------------------------------------------------------
// Emscripten WASM video-export — C binding implementations.
//
// A global PixelPaintView pointer is registered by the view's
// constructor so these free C functions can reach the timeline
// without dragging every header into JS glue code.
//
// Compiled only when building for Emscripten / WASM.
// On all other targets this translation unit is empty.
// ---------------------------------------------------------------

#ifdef __EMSCRIPTEN__

#include "VideoExport.hpp"
#include "../PixelPaintView.hpp"

#include <emscripten.h>
#include <cstring>
#include <cstdio>

namespace pelpaint {
    /// Set by PixelPaintView's constructor; cleared by its destructor.
    PixelPaintView* g_VideoExportView = nullptr;
}

// ----------------------------------------------------------------
// C bindings
// ----------------------------------------------------------------

extern "C" {

EMSCRIPTEN_KEEPALIVE int video_export_frame_count()
{
    if (!pelpaint::g_VideoExportView) return 0;
    return pelpaint::g_VideoExportView->VideoGetFrameCount();
}

EMSCRIPTEN_KEEPALIVE int video_export_frame_width()
{
    if (!pelpaint::g_VideoExportView) return 0;
    return pelpaint::g_VideoExportView->VideoGetWidth();
}

EMSCRIPTEN_KEEPALIVE int video_export_frame_height()
{
    if (!pelpaint::g_VideoExportView) return 0;
    return pelpaint::g_VideoExportView->VideoGetHeight();
}

EMSCRIPTEN_KEEPALIVE float video_export_fps()
{
    if (!pelpaint::g_VideoExportView) return 12.f;
    return pelpaint::g_VideoExportView->VideoGetFps();
}

EMSCRIPTEN_KEEPALIVE int video_export_get_frame(int frameIdx, uint8_t* outRGBA, int bufLen)
{
    if (!pelpaint::g_VideoExportView || !outRGBA || bufLen < 4) return 0;
    return pelpaint::g_VideoExportView->VideoGetFrameRGBA(frameIdx, outRGBA, bufLen) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void video_export()
{
    // Hand off to the JS side.  Module.startVideoExport() is defined in
    // shell.html and drives the full MediaRecorder capture loop.
    EM_ASM({
        if (typeof Module.startVideoExport === 'function') {
            Module.startVideoExport();
        } else {
            console.error('[pelpaint] Module.startVideoExport is not defined. '
                          'Check shell.html for the JS implementation.');
        }
    });
}

} // extern "C"
#endif // __EMSCRIPTEN__
