#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <random>
#include "../core/Types.hpp"

namespace pelpaint::tools {

class PixelPerfectGenerator {
public:
    /// Palette-indexed pixel buffer -- the fundamental output type.
    struct PixelPerfect {
        int               width  = 0;
        int               height = 0;
        std::vector<Pixel> pixels;
        std::vector<char>  indices;   ///< palette key per pixel
        std::map<char, Pixel> palette;

        std::string ToAsciiArt() const;
    };

    /// L-System turtle-graphics configuration.
    struct LSysConfig {
        std::string              axiom;
        std::map<char, std::string> rules;
        float                    angle      = 25.f;
        int                      iterations = 4;
        std::map<std::string, Pixel> colors;
    };

    /// Cellular-automata (cave / terrain) configuration.
    struct CellularConfig {
        int   birth_rule   = 3;
        int   survive_rule = 4;
        float initial_fill = 0.45f;
        int   iterations   = 5;
        Pixel alive_color  = { 200, 200, 200, 255 };
        Pixel dead_color   = {  30,  30,  30, 255 };
    };

    /// Repeating-pattern stamp configuration.
    struct PatternConfig {
        std::vector<std::string> pattern;
        std::map<char, Pixel>    colors;
        bool repeat_x = true;
        bool repeat_y = true;
    };

    static PixelPerfect GenerateLSystem (const LSysConfig&     config, int width, int height);
    static PixelPerfect GenerateCellular(const CellularConfig& config, int width, int height);
    static PixelPerfect GeneratePattern (const PatternConfig&  config, int width, int height);
    static PixelPerfect GenerateNoise   (int width, int height, float scale,
                                         const std::vector<Pixel>& gradient);

    static std::vector<PixelPerfect> GenerateAnimation(
        const PixelPerfect&                    base,
        const std::vector<std::string>&        modifyPattern,
        const std::vector<std::pair<int,int>>& region);

    static PixelPerfect CyclePalette       (const PixelPerfect& art, float time, float speed,
                                            const std::vector<char>& targetKeys);
    static PixelPerfect ApplyWaveEffect    (const PixelPerfect& art, float time,
                                            float amplitude, float frequency,
                                            bool horizontal = true);
    static PixelPerfect ApplyPulseEffect   (const PixelPerfect& art, float time,
                                            float amplitude, float frequency,
                                            bool horizontal = false);
    static PixelPerfect ApplyOrbitalEffect (const PixelPerfect& art, float time,
                                            float amplitude, float frequency,
                                            bool horizontal = true);
    static PixelPerfect ApplySineWaveEffect(const PixelPerfect& art, float time,
                                            float amplitude, float frequency,
                                            bool horizontal = true);
    static PixelPerfect CreateTiled(const PixelPerfect& art, int multiplier_x, int multiplier_y);

    static PixelPerfect Resize (const PixelPerfect& art, int new_width, int new_height);
    static PixelPerfect Rotate (const PixelPerfect& art, float angle);
    static PixelPerfect Mirror (const PixelPerfect& art, bool horizontal = true);
    static PixelPerfect Compose(const std::vector<PixelPerfect>& layers);

    /// Advance the shared RNG so that the next Generate call produces
    /// a different pseudo-random layout without changing any parameters.
    static std::mt19937& GetRNGPublic() { return GetRNG(); }

private:
    static float Perlin2D(float x, float y, float scale);
    static void  DrawLSystemBranch(std::vector<Pixel>& pixels, int width, int height,
                                   float x, float y, float angle, float length,
                                   const Pixel& color);
    static int   CountNeighbors(const std::vector<bool>& grid,
                                int width, int height, int x, int y);

    static std::mt19937& GetRNG() {
        static std::random_device rd;
        static std::mt19937       gen(rd());
        return gen;
    }
};

using PixelPerfectPtr = std::shared_ptr<PixelPerfectGenerator::PixelPerfect>;
using GeneratorFunc   = std::function<PixelPerfectPtr(int width, int height)>;

} // namespace pelpaint::tools
