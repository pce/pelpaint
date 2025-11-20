#pragma once

#include <vector>
#include <cstdint>
#include <string>

// Represent a single pixel (RGBA)
struct Pixel {
    uint8_t r, g, b, a;
    
    Pixel() : r(0), g(0), b(0), a(255) {}
    Pixel(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}
    
    // Equality operator
    bool operator==(const Pixel& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    
    // Calculate color distance (squared Euclidean distance in RGB space)
    int DistanceSquared(const Pixel& other) const {
        int dr = static_cast<int>(r) - static_cast<int>(other.r);
        int dg = static_cast<int>(g) - static_cast<int>(other.g);
        int db = static_cast<int>(b) - static_cast<int>(other.b);
        return dr * dr + dg * dg + db * db;
    }
    
    // Find nearest color in a palette
    static Pixel FindNearest(const Pixel& color, const std::vector<Pixel>& palette) {
        if (palette.empty()) return color;
        
        int minDist = color.DistanceSquared(palette[0]);
        size_t nearestIdx = 0;
        
        for (size_t i = 1; i < palette.size(); ++i) {
            int dist = color.DistanceSquared(palette[i]);
            if (dist < minDist) {
                minDist = dist;
                nearestIdx = i;
            }
        }
        
        return palette[nearestIdx];
    }
};

// Palette structure with metadata
struct ColorPalette {
    std::string name;
    std::vector<Pixel> colors;
    std::string description;
    
    ColorPalette(const std::string& n, const std::vector<Pixel>& c, const std::string& desc = "")
        : name(n), colors(c), description(desc) {}
};

namespace ColorPalettes {

// PICO-8 Palette (16 colors)
inline const std::vector<Pixel> PICO8 = {
    {0, 0, 0, 255},       // 0: Black
    {29, 43, 83, 255},    // 1: Dark Blue
    {126, 37, 83, 255},   // 2: Dark Purple
    {0, 135, 81, 255},    // 3: Dark Green
    {171, 82, 54, 255},   // 4: Brown
    {95, 87, 79, 255},    // 5: Dark Gray
    {194, 195, 199, 255}, // 6: Light Gray
    {255, 241, 232, 255}, // 7: White
    {255, 0, 77, 255},    // 8: Red
    {255, 163, 0, 255},   // 9: Orange
    {255, 236, 39, 255},  // 10: Yellow
    {0, 228, 54, 255},    // 11: Green
    {41, 173, 255, 255},  // 12: Blue
    {131, 118, 156, 255}, // 13: Indigo
    {255, 119, 168, 255}, // 14: Pink
    {255, 204, 170, 255}  // 15: Peach
};

// Game Boy Classic (4 colors)
inline const std::vector<Pixel> GAMEBOY = {
    {15, 56, 15, 255},    // Darkest
    {48, 98, 48, 255},    // Dark
    {139, 172, 15, 255},  // Light
    {155, 188, 15, 255}   // Lightest
};

// Game Boy Pocket (4 colors - black & white)
inline const std::vector<Pixel> GAMEBOY_POCKET = {
    {0, 0, 0, 255},       // Black
    {85, 85, 85, 255},    // Dark Gray
    {170, 170, 170, 255}, // Light Gray
    {255, 255, 255, 255}  // White
};

// NES Palette (subset of most used colors)
inline const std::vector<Pixel> NES = {
    {0, 0, 0, 255},       // Black
    {124, 124, 124, 255}, // Gray
    {248, 248, 248, 255}, // White
    {252, 60, 0, 255},    // Red
    {252, 116, 60, 255},  // Orange
    {252, 184, 0, 255},   // Yellow
    {60, 188, 0, 255},    // Green
    {0, 188, 188, 255},   // Cyan
    {0, 120, 248, 255},   // Blue
    {104, 68, 252, 255},  // Purple
    {216, 0, 204, 255},   // Magenta
    {148, 0, 0, 255},     // Dark Red
    {0, 88, 0, 255},      // Dark Green
    {0, 64, 88, 255}      // Dark Blue
};

// Commodore 64 Palette (16 colors)
inline const std::vector<Pixel> C64 = {
    {0, 0, 0, 255},       // Black
    {255, 255, 255, 255}, // White
    {136, 57, 50, 255},   // Red
    {103, 182, 189, 255}, // Cyan
    {139, 63, 150, 255},  // Purple
    {85, 160, 73, 255},   // Green
    {64, 49, 141, 255},   // Blue
    {191, 206, 114, 255}, // Yellow
    {139, 84, 41, 255},   // Orange
    {87, 66, 0, 255},     // Brown
    {184, 105, 98, 255},  // Light Red
    {80, 80, 80, 255},    // Dark Gray
    {120, 120, 120, 255}, // Gray
    {148, 224, 137, 255}, // Light Green
    {120, 105, 196, 255}, // Light Blue
    {159, 159, 159, 255}  // Light Gray
};

// Apple II (6 colors)
inline const std::vector<Pixel> APPLE_II = {
    {0, 0, 0, 255},       // Black
    {114, 38, 64, 255},   // Purple/Magenta
    {64, 51, 127, 255},   // Blue
    {20, 207, 253, 255},  // Light Blue
    {20, 245, 60, 255},   // Green
    {255, 255, 255, 255}  // White
};

// CGA Palette (16 colors)
inline const std::vector<Pixel> CGA = {
    {0, 0, 0, 255},       // Black
    {0, 0, 170, 255},     // Blue
    {0, 170, 0, 255},     // Green
    {0, 170, 170, 255},   // Cyan
    {170, 0, 0, 255},     // Red
    {170, 0, 170, 255},   // Magenta
    {170, 85, 0, 255},    // Brown
    {170, 170, 170, 255}, // Light Gray
    {85, 85, 85, 255},    // Dark Gray
    {85, 85, 255, 255},   // Light Blue
    {85, 255, 85, 255},   // Light Green
    {85, 255, 255, 255},  // Light Cyan
    {255, 85, 85, 255},   // Light Red
    {255, 85, 255, 255},  // Light Magenta
    {255, 255, 85, 255},  // Yellow
    {255, 255, 255, 255}  // White
};

// ZX Spectrum Palette (15 colors)
inline const std::vector<Pixel> ZX_SPECTRUM = {
    {0, 0, 0, 255},       // Black
    {0, 0, 215, 255},     // Blue
    {215, 0, 0, 255},     // Red
    {215, 0, 215, 255},   // Magenta
    {0, 215, 0, 255},     // Green
    {0, 215, 215, 255},   // Cyan
    {215, 215, 0, 255},   // Yellow
    {215, 215, 215, 255}, // White
    {0, 0, 255, 255},     // Bright Blue
    {255, 0, 0, 255},     // Bright Red
    {255, 0, 255, 255},   // Bright Magenta
    {0, 255, 0, 255},     // Bright Green
    {0, 255, 255, 255},   // Bright Cyan
    {255, 255, 0, 255},   // Bright Yellow
    {255, 255, 255, 255}  // Bright White
};

// Grayscale (16 shades)
inline const std::vector<Pixel> GRAYSCALE_16 = {
    {0, 0, 0, 255}, {17, 17, 17, 255}, {34, 34, 34, 255}, {51, 51, 51, 255},
    {68, 68, 68, 255}, {85, 85, 85, 255}, {102, 102, 102, 255}, {119, 119, 119, 255},
    {136, 136, 136, 255}, {153, 153, 153, 255}, {170, 170, 170, 255}, {187, 187, 187, 255},
    {204, 204, 204, 255}, {221, 221, 221, 255}, {238, 238, 238, 255}, {255, 255, 255, 255}
};

// Monochrome (2 colors)
inline const std::vector<Pixel> MONOCHROME = {
    {0, 0, 0, 255},
    {255, 255, 255, 255}
};

// Teletext Palette (8 colors)
inline const std::vector<Pixel> TELETEXT = {
    {0, 0, 0, 255},       // Black
    {255, 0, 0, 255},     // Red
    {0, 255, 0, 255},     // Green
    {255, 255, 0, 255},   // Yellow
    {0, 0, 255, 255},     // Blue
    {255, 0, 255, 255},   // Magenta
    {0, 255, 255, 255},   // Cyan
    {255, 255, 255, 255}  // White
};

// Amstrad CPC (27 colors - basic set)
inline const std::vector<Pixel> AMSTRAD_CPC = {
    {0, 0, 0, 255},       // Black
    {0, 0, 128, 255},     // Blue
    {0, 0, 255, 255},     // Bright Blue
    {128, 0, 0, 255},     // Red
    {128, 0, 128, 255},   // Magenta
    {128, 0, 255, 255},   // Mauve
    {255, 0, 0, 255},     // Bright Red
    {255, 0, 128, 255},   // Purple
    {255, 0, 255, 255},   // Bright Magenta
    {0, 128, 0, 255},     // Green
    {0, 128, 128, 255},   // Cyan
    {0, 128, 255, 255},   // Sky Blue
    {128, 128, 0, 255},   // Yellow
    {128, 128, 128, 255}, // White
    {128, 128, 255, 255}, // Pastel Blue
    {255, 128, 0, 255},   // Orange
    {255, 128, 128, 255}, // Pink
    {255, 128, 255, 255}, // Pastel Magenta
    {0, 255, 0, 255},     // Bright Green
    {0, 255, 128, 255},   // Sea Green
    {0, 255, 255, 255},   // Bright Cyan
    {128, 255, 0, 255},   // Lime
    {128, 255, 128, 255}, // Pastel Green
    {128, 255, 255, 255}, // Pastel Cyan
    {255, 255, 0, 255},   // Bright Yellow
    {255, 255, 128, 255}, // Pastel Yellow
    {255, 255, 255, 255}  // Bright White
};

// DB32 (DawnBringer's 32 Color Palette - popular for pixel art)
inline const std::vector<Pixel> DB32 = {
    {0, 0, 0, 255},       {34, 32, 52, 255},    {69, 40, 60, 255},    {102, 57, 49, 255},
    {143, 86, 59, 255},   {223, 113, 38, 255},  {217, 160, 102, 255}, {238, 195, 154, 255},
    {251, 242, 54, 255},  {153, 229, 80, 255},  {106, 190, 48, 255},  {55, 148, 110, 255},
    {75, 105, 47, 255},   {82, 75, 36, 255},    {50, 60, 57, 255},    {63, 63, 116, 255},
    {48, 96, 130, 255},   {91, 110, 225, 255},  {99, 155, 255, 255},  {95, 205, 228, 255},
    {203, 219, 252, 255}, {255, 255, 255, 255}, {155, 173, 183, 255}, {132, 126, 135, 255},
    {105, 106, 106, 255}, {89, 86, 82, 255},    {118, 66, 138, 255},  {172, 50, 50, 255},
    {217, 87, 99, 255},   {215, 123, 186, 255}, {143, 151, 74, 255},  {138, 111, 48, 255}
};

// AAP-64 (Adigun A. Polack's 64 color palette)
inline const std::vector<Pixel> AAP64 = {
    {6, 6, 8, 255},       {20, 16, 19, 255},    {59, 23, 37, 255},    {115, 23, 45, 255},
    {180, 32, 42, 255},   {223, 62, 35, 255},   {250, 106, 10, 255},  {249, 163, 27, 255},
    {255, 213, 65, 255},  {232, 255, 120, 255}, {148, 250, 95, 255},  {68, 227, 105, 255},
    {34, 184, 126, 255},  {20, 128, 126, 255},  {13, 87, 97, 255},    {15, 59, 71, 255},
    {18, 32, 46, 255},    {24, 50, 90, 255},    {41, 87, 140, 255},   {63, 123, 201, 255},
    {99, 155, 255, 255},  {139, 191, 255, 255}, {192, 224, 248, 255}, {235, 242, 255, 255},
    {255, 255, 255, 255}, {201, 212, 220, 255}, {150, 163, 176, 255}, {107, 121, 140, 255},
    {60, 68, 84, 255},    {38, 43, 58, 255},    {60, 40, 60, 255},    {105, 61, 76, 255},
    {156, 79, 85, 255},   {207, 110, 95, 255},  {238, 158, 111, 255}, {255, 206, 150, 255},
    {255, 231, 201, 255}, {224, 185, 162, 255}, {181, 143, 128, 255}, {129, 98, 96, 255},
    {86, 66, 68, 255},    {57, 42, 49, 255},    {105, 86, 58, 255},   {148, 133, 62, 255},
    {189, 171, 54, 255},  {227, 220, 86, 255},  {178, 107, 56, 255},  {140, 72, 56, 255},
    {102, 50, 56, 255},   {130, 29, 57, 255},   {109, 42, 88, 255},   {98, 51, 129, 255},
    {92, 66, 166, 255},   {92, 85, 190, 255},   {89, 111, 213, 255},  {83, 139, 219, 255},
    {77, 171, 247, 255},  {82, 206, 240, 255},  {97, 222, 210, 255},  {126, 234, 179, 255},
    {162, 240, 148, 255}, {203, 246, 125, 255}, {242, 251, 125, 255}, {254, 232, 146, 255}
};

// All palettes organized
inline std::vector<ColorPalette> GetAllPalettes() {
    return {
        {"PICO-8", PICO8, "Fantasy console palette (16 colors)"},
        {"Game Boy", GAMEBOY, "Original Game Boy green palette (4 colors)"},
        {"Game Boy Pocket", GAMEBOY_POCKET, "Game Boy Pocket monochrome (4 colors)"},
        {"NES", NES, "Nintendo Entertainment System (14 colors)"},
        {"Commodore 64", C64, "C64 16 color palette"},
        {"Apple II", APPLE_II, "Apple II hi-res colors (6 colors)"},
        {"CGA", CGA, "IBM Color Graphics Adapter (16 colors)"},
        {"ZX Spectrum", ZX_SPECTRUM, "Sinclair ZX Spectrum (15 colors)"},
        {"Teletext", TELETEXT, "Teletext/ANSI colors (8 colors)"},
        {"Amstrad CPC", AMSTRAD_CPC, "Amstrad CPC basic palette (27 colors)"},
        {"DB32", DB32, "DawnBringer's 32 color palette"},
        {"AAP-64", AAP64, "Adigun A. Polack's 64 color palette"},
        {"Grayscale 16", GRAYSCALE_16, "16 shades of gray"},
        {"Monochrome", MONOCHROME, "Pure black & white (2 colors)"}
    };
}

// Get palette by name
inline const std::vector<Pixel>* GetPaletteByName(const std::string& name) {
    if (name == "PICO-8") return &PICO8;
    if (name == "Game Boy") return &GAMEBOY;
    if (name == "Game Boy Pocket") return &GAMEBOY_POCKET;
    if (name == "NES") return &NES;
    if (name == "Commodore 64") return &C64;
    if (name == "Apple II") return &APPLE_II;
    if (name == "CGA") return &CGA;
    if (name == "ZX Spectrum") return &ZX_SPECTRUM;
    if (name == "Teletext") return &TELETEXT;
    if (name == "Amstrad CPC") return &AMSTRAD_CPC;
    if (name == "DB32") return &DB32;
    if (name == "AAP-64") return &AAP64;
    if (name == "Grayscale 16") return &GRAYSCALE_16;
    if (name == "Monochrome") return &MONOCHROME;
    return nullptr;
}

} // namespace ColorPalettes