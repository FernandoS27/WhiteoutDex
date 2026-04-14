#pragma once
// ============================================================================
// WhiteoutDex Renderer — PE1 System (Model Particle Emitter)
// Simulates particles that spawn MDX model instances.
// Each particle carries a position, velocity, lifetime, and a handle
// to its child ModelInstance (managed by the Renderer).
// ============================================================================

#include "types.h"
#include "model_types.h"
#include <unordered_map>
#include <vector>
#include <random>

namespace WhiteoutDex {

// ============================================================================
// PE1 Emitter State (per-frame animatable values)
// ============================================================================
struct PE1EmitterState {
    Matrix44f transform = Matrix44f::identity();
    float emissionRate = 0;
    float speed        = 0;
    float latitude     = 0;  // radians
    float longitude    = 0;  // radians
    float gravity      = 0;
    float visibility   = 1.0f;
};

// ============================================================================
// PE1 Particle — individual model particle instance
// ============================================================================
struct PE1Particle {
    Vector3f position  = {0,0,0};
    Vector3f velocity  = {0,0,0};
    float    lifeSpan  = 0;       // remaining
    float    initLife   = 0;      // original
    uint32_t childModelHandle = 0;
    int      emitterId = 0;       // which emitter spawned this
};

// ============================================================================
// PE1 Emitter — config + state + live particles
// ============================================================================
struct PE1Emitter {
    PE1EmitterConfig  config;
    PE1EmitterState   state;
    std::vector<PE1Particle> particles;
    float accumEmission = 0;
};

// ============================================================================
// Simulation result — birth/death/transform events for the renderer
// ============================================================================
struct PE1BirthEvent {
    uint32_t handle;
    int emitterId;
    Matrix44f worldTransform;
};

struct PE1SimResult {
    std::vector<PE1BirthEvent> born;
    std::vector<uint32_t> died;
    std::vector<std::pair<uint32_t, Matrix44f>> transforms;
};

// ============================================================================
// PE1System — manages all PE1 emitters for one model instance
// ============================================================================
class PE1System {
public:
    void Clear();
    void AddEmitter(int id, const PE1EmitterConfig& cfg);
    void UpdateEmitterState(int id, const PE1EmitterState& st);
    bool HasEmitters() const;
    int  GetTotalParticleCount() const;
    const PE1EmitterConfig* GetConfig(int emitterId) const;

    // Simulate one time step. Allocates handles from nextHandle (by reference).
    PE1SimResult Simulate(float dt, uint32_t& nextHandle);

private:
    void SpawnParticle(PE1Emitter& em, float dt, uint32_t& nextHandle, PE1SimResult& result);
    float RandF(float lo, float hi);

    std::unordered_map<int, PE1Emitter> emitters_;
    std::mt19937 rng_{std::random_device{}()};
};

} // namespace WhiteoutDex
