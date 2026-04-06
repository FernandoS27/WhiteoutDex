#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Particle System (BlizzParticle2)
// Phase 5: Camera-facing billboard particles with 3-segment color interpolation
//
// Ported from Magos Particle.cpp / ParticleManager.cpp
// Emitter config sent once, animatable values + transform sent per frame.
// DLL simulates particles at 60fps independently of Max.
// ============================================================================

#include "types.h"
#include <unordered_map>
#include <random>
#include <cmath>
#include <algorithm>

namespace WhiteoutDex {

// ============================================================================
// Individual particle instance
// ============================================================================
struct Particle {
    XMFLOAT3 position  = {0,0,0};
    XMFLOAT3 velocity  = {0,0,0};
    float    lifeSpan  = 0;       // remaining
    float    initLife   = 0;       // original
};

// ============================================================================
// Static emitter configuration (sent once from MaxScript)
// ============================================================================
struct ParticleEmitterConfig {
    int   textureId    = -1;
    int   filterMode   = 0;      // 0=None,1=Transparent,2=Blend,3=Additive,4=AddAlpha,5=Modulate
    int   rows = 1, cols = 1;
    bool  unshaded     = false;

    float lifeSpan     = 1.0f;
    bool  squirt       = false;

    // 3-segment color/alpha/scale
    XMFLOAT3 startColor  = {1,1,1};
    XMFLOAT3 midColor    = {0.5f,0.5f,0.5f};
    XMFLOAT3 endColor    = {0,0,0};
    float startAlpha = 255, midAlpha = 128, endAlpha = 0;
    float startScale = 10, midScale = 10, endScale = 10;
    float midTime    = 0.5f;

    // Head/Tail
    int   particleType = 1;   // 1=Head, 2=Tail, 3=Both
    float tailLength   = 1.0f;

    // UV animation frames
    int headLifeStart=0, headLifeEnd=0, headLifeRepeat=1;
    int headDecayStart=0, headDecayEnd=0, headDecayRepeat=1;
    int tailLifeStart=0, tailLifeEnd=0, tailLifeRepeat=1;
    int tailDecayStart=0, tailDecayEnd=0, tailDecayRepeat=1;

    // Flags
    bool modelSpace = false;
    bool xyQuad     = false;
    bool sortZ      = false;
};

// ============================================================================
// Per-frame emitter state (animatable values + transform from MaxScript)
// ============================================================================
struct ParticleEmitterState {
    XMMATRIX transform = XMMatrixIdentity();
    float emissionRate = 0;
    float speed        = 0;
    float variation    = 0;
    float coneAngle    = 0;    // degrees
    float gravity      = 0;
    float width        = 0;
    float length       = 0;
    float visibility   = 1.0f;
};

// ============================================================================
// Emitter instance (config + state + live particles)
// ============================================================================
struct ParticleEmitter {
    ParticleEmitterConfig  config;
    ParticleEmitterState   state;
    std::vector<Particle>  particles;
    float                  accumEmission = 0;
    bool                   squirtDone    = false;
};

// ============================================================================
// Particle System — manages all emitters, simulates & renders particles
// ============================================================================
class ParticleSystem {
public:
    void Clear();
    void AddEmitter(int id, const ParticleEmitterConfig& cfg);
    void UpdateEmitterState(int id, const ParticleEmitterState& st);

    bool HasEmitters() const;
    int  EmitterCount() const;

    void Simulate(float dt);

    int BuildBillboards(float cameraPitch, float cameraYaw,
                        std::vector<Vertex>& outVerts,
                        std::vector<int>& outEmitterIds) const;

    const ParticleEmitterConfig* GetConfig(int id) const;
    int GetTotalParticleCount() const;

    bool GetEmitterVertexRange(int emitterId, int totalVerts,
                               const std::vector<int>& emitterIds,
                               int& outStart, int& outCount) const;

private:
    void SpawnParticle(ParticleEmitter& em);

    static void Interpolate3Seg(const ParticleEmitterConfig& cfg, float t,
                                 XMFLOAT3& outColor, float& outAlpha, float& outScale);

    static void ComputeUV(const ParticleEmitterConfig& cfg, float lifeFactor,
                           bool isHead, float& u0, float& v0, float& u1, float& v1);

    static XMFLOAT3 Lerp3(const XMFLOAT3& a, const XMFLOAT3& b, float t);

    float RandF(float lo, float hi);

    std::unordered_map<int, ParticleEmitter> emitters_;
    std::mt19937 rng_{std::random_device{}()};
};

} // namespace WhiteoutDex
