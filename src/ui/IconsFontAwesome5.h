// IconsFontAwesome5.h — minimal Font Awesome 5 Free (Solid) subset for PelPaint
//
// Source:  https://github.com/FortAwesome/Font-Awesome  (SIL OFL 1.1)
// Helper:  https://github.com/juliettef/IconFontCppHeaders
//
// UTF-8 sequences are derived from the Unicode code-points of each glyph.
// All FA5 free-solid icons live in [U+F000 .. U+F8FF] — see ICON_MIN/MAX_FA5.
//
// Usage:
//   1. Call pelpaint::ui::LoadFonts() once after ImGui::CreateContext().
//   2. Use ICON_FA_* string literals as ImGui::Button() labels or in text.
//
// Only the icons actually used by PelPaint are listed here
// Look up the code-point at https://fontawesome.com/icons?d=gallery&s=solid&m=free

#pragma once

// ── Glyph range that covers the entire FA5 free-solid set ─────────────────────
#define ICON_MIN_FA5  0xf000
#define ICON_MAX_FA5  0xf8ff

// ── Font file name (must exist in the bundled fonts/ directory) ───────────────
#define FONT_ICON_FILE_NAME_FA5S  "fa-solid-900.ttf"

// ── Icons used in the left-side toolbar ───────────────────────────────────────
// U+F303  pencil-alt   — Pencil / freehand draw
#define ICON_FA_PENCIL_ALT     "\xef\x8c\x83"
// U+F12D  eraser        — Eraser tool
#define ICON_FA_ERASER         "\xef\x84\xad"
// U+F715  slash         — Line / straight-line tool
#define ICON_FA_SLASH          "\xef\x9c\x95"
// U+F043  tint          — Gradient/blend fill
#define ICON_FA_TINT           "\xef\x81\x83"
// U+F1FB  eye-dropper   — Eyedropper / colour picker
#define ICON_FA_EYE_DROPPER    "\xef\x87\xbb"
// U+F0D0  magic         — Spray / airbrush (best free proxy)
#define ICON_FA_MAGIC          "\xef\x83\x90"
// U+F5CB  vector-square — Rectangle selection (dashed bounding box)
#define ICON_FA_VECTOR_SQUARE  "\xef\x97\x8b"
// U+F576  fill-drip     — Bucket fill
#define ICON_FA_FILL_DRIP      "\xef\x95\xb6"

// ── Extra icons (selection sub-modes, right-panel tool entries) ───────────────
// U+F111  circle        — Circle selection
#define ICON_FA_CIRCLE         "\xef\x84\x91"
// U+F5EE  draw-polygon  — Polygon lasso selection
#define ICON_FA_DRAW_POLYGON   "\xef\x97\xae"
// U+F5BF  stamp         — Clone stamp
#define ICON_FA_STAMP          "\xef\x96\xbf"
// U+F61F  shapes        — Shape redraw
#define ICON_FA_SHAPES         "\xef\x98\x9f"
// U+F1FC  paint-brush   — Brush / paint tool
#define ICON_FA_PAINT_BRUSH    "\xef\x87\xbc"
// U+F065  expand        — Expand / full-screen (utility)
#define ICON_FA_EXPAND         "\xef\x81\xa5"
