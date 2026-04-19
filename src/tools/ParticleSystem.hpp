#pragma once

#include <vector>
#include "../core/Types.hpp"

namespace pelpaint::tools {

struct Particle {
    Point2f  position;
    Point2f  velocity;
    Pixel    color;
    float    life    = 0.f;
    float    maxLife = 1.f;
    float    size    = 2.f;
    bool     active  = false;
};

struct ParticleConfig {
    int     maxParticles = 200;
    float   emissionRate = 50.f;
    Point2f minVelocity  = { -30.f, -30.f };
    Point2f maxVelocity  = {  30.f,  30.f };
    Pixel   startColor   = { 255, 255, 255, 255 };
    Pixel   endColor     = { 255, 255, 255,   0 };
    float   minLife      = 0.8f;
    float   maxLife      = 2.0f;
    float   minSize      = 1.f;
    float   maxSize      = 4.f;
    bool    loop         = false;
};

class ParticleSystem {
public:
    ParticleSystem();

    void Init(const ParticleConfig& config);

    void Update(float deltaTime, Point2f emitterPos);

    void DrawToBuffer(std::vector<Pixel>& buffer, int width, int height) const;

    bool IsFinished() const { return isFinished; }
    const std::vector<Particle>& GetParticles() const { return particles; }

private:
    void Emit(Point2f position);

    ParticleConfig        config;
    std::vector<Particle> particles;
    float                 emissionTimer;
    bool                  isFinished;
};

} // namespace pelpaint::tools
