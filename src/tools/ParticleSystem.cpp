#include "ParticleSystem.hpp"
#include <cmath>
#include <algorithm>
#include <random>

namespace pelpaint::tools {

// Internal RNG -- replaces the old raylib GetRandomValue()
namespace {
    int RandInt(int mn, int mx) {
        static std::mt19937 rng{ std::random_device{}() };
        if (mn >= mx) return mn;
        return std::uniform_int_distribution<int>(mn, mx)(rng);
    }
} // anonymous namespace

ParticleSystem::ParticleSystem()
    : emissionTimer(0.f)
    , isFinished(false)
{}

void ParticleSystem::Init(const ParticleConfig& cfg) {
    config = cfg;
    particles.resize(static_cast<std::size_t>(cfg.maxParticles));
    for (auto& p : particles) p.active = false;
    emissionTimer = 0.f;
    isFinished    = false;
}

void ParticleSystem::Update(float dt, Point2f emitterPos) {
    bool anyActive = false;
    for (auto& p : particles) {
        if (!p.active) continue;
        anyActive = true;

        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.life -= dt;

        if (p.life <= 0.f) {
            p.active = false;
        } else {
            float t = 1.f - (p.life / p.maxLife);
            auto lerp = [](uint8_t a, uint8_t b, float tt) -> uint8_t {
                return static_cast<uint8_t>(a + tt * (static_cast<float>(b) - a));
            };
            p.color.r = lerp(config.startColor.r, config.endColor.r, t);
            p.color.g = lerp(config.startColor.g, config.endColor.g, t);
            p.color.b = lerp(config.startColor.b, config.endColor.b, t);
            p.color.a = lerp(config.startColor.a, config.endColor.a, t);
        }
    }

    if (config.loop || !isFinished) {
        emissionTimer += dt;
        const float interval = 1.f / config.emissionRate;
        while (emissionTimer >= interval) {
            Emit(emitterPos);
            emissionTimer -= interval;
        }
    }

    if (!config.loop && !anyActive) isFinished = true;
}

void ParticleSystem::Emit(Point2f pos) {
    for (auto& p : particles) {
        if (p.active) continue;
        p.active     = true;
        p.position   = pos;
        p.velocity.x = RandInt(static_cast<int>(config.minVelocity.x * 100),
                                static_cast<int>(config.maxVelocity.x * 100)) / 100.f;
        p.velocity.y = RandInt(static_cast<int>(config.minVelocity.y * 100),
                                static_cast<int>(config.maxVelocity.y * 100)) / 100.f;
        p.maxLife    = RandInt(static_cast<int>(config.minLife * 100),
                               static_cast<int>(config.maxLife * 100)) / 100.f;
        p.life       = p.maxLife;
        p.size       = RandInt(static_cast<int>(config.minSize * 100),
                               static_cast<int>(config.maxSize * 100)) / 100.f;
        p.color      = config.startColor;
        break;
    }
}

void ParticleSystem::DrawToBuffer(std::vector<Pixel>& buffer, int w, int h) const {
    for (const auto& p : particles) {
        if (!p.active) continue;
        const int sz = std::max(1, static_cast<int>(p.size));
        const int px = static_cast<int>(p.position.x);
        const int py = static_cast<int>(p.position.y);
        for (int dy = 0; dy < sz; ++dy) {
            for (int dx = 0; dx < sz; ++dx) {
                const int nx = px + dx, ny = py + dy;
                if (nx >= 0 && nx < w && ny >= 0 && ny < h)
                    buffer[static_cast<std::size_t>(ny * w + nx)] = p.color;
            }
        }
    }
}

} // namespace pelpaint::tools
