// ============================================================================
// WhiteoutDex Renderer — PE1 System Implementation
// Model particle emitter simulation.
// ============================================================================

#include "pe1_system.h"
#include "sim_util.h"
#include <cmath>
#include <algorithm>

namespace WhiteoutDex {

void PE1System::Clear() {
    emitters_.clear();
}

void PE1System::AddEmitter(int id, const PE1EmitterConfig& cfg) {
    emitters_[id].config = cfg;
    emitters_[id].particles.clear();
    emitters_[id].accumEmission = 0;
}

void PE1System::UpdateEmitterState(int id, const PE1EmitterState& st) {
    auto it = emitters_.find(id);
    if (it != emitters_.end())
        it->second.state = st;
}

bool PE1System::HasEmitters() const { return !emitters_.empty(); }

int PE1System::GetTotalParticleCount() const {
    int total = 0;
    for (auto& [id, em] : emitters_) total += (int)em.particles.size();
    return total;
}

const PE1EmitterConfig* PE1System::GetConfig(int emitterId) const {
    auto it = emitters_.find(emitterId);
    return (it != emitters_.end()) ? &it->second.config : nullptr;
}

// ============================================================================
// Simulation
// ============================================================================

PE1SimResult PE1System::Simulate(float dt, uint32_t& nextHandle) {
    PE1SimResult result;
    dt = ClampDeltaTime(dt);

    for (auto& [id, em] : emitters_) {
        // Remove dead particles
        for (auto it = em.particles.begin(); it != em.particles.end(); ) {
            if (it->lifeSpan <= 0) {
                result.died.push_back(it->childModelHandle);
                it = em.particles.erase(it);
            } else {
                ++it;
            }
        }

        // Emission
        if (IsEmitterVisible(em.state.visibility) && em.state.emissionRate > 0) {
            em.accumEmission += em.state.emissionRate * dt;
            while (em.accumEmission >= 1.0f) {
                SpawnParticle(em, dt, nextHandle, result);
                em.accumEmission -= 1.0f;
            }
        }

        // Update existing particles: Euler integration with gravity
        float az = -(em.state.gravity);
        for (auto& p : em.particles) {
            p.position.x += p.velocity.x * dt;
            p.position.y += p.velocity.y * dt;
            p.position.z += p.velocity.z * dt + 0.5f * az * dt * dt;
            p.velocity.z += az * dt;
            p.lifeSpan -= dt;

            // Update world transform for this particle's child model
            XMMATRIX scale = XMMatrixScaling(em.config.scale, em.config.scale, em.config.scale);
            XMMATRIX trans = XMMatrixTranslation(p.position.x, p.position.y, p.position.z);
            result.transforms.push_back({p.childModelHandle, scale * trans});
        }
    }

    return result;
}

// ============================================================================
// Spawn a PE1 particle
// ============================================================================

void PE1System::SpawnParticle(PE1Emitter& em, float dt,
                               uint32_t& nextHandle, PE1SimResult& result) {
    PE1Particle p;
    auto& cfg = em.config;
    auto& st = em.state;

    // Position: emitter origin in world space
    XMVECTOR origin = XMVectorSet(0, 0, 0, 1);
    XMVECTOR wPos = XMVector4Transform(origin, st.transform);
    XMStoreFloat3(&p.position, wPos);

    // Velocity cone (matches engine CParticleEmitter::CreateParticle)
    // lat/lon are in radians (MDX adapter passes directly, Max adapter converts deg→rad)
    float theta = (2.0f * st.latitude  * RandF(0.0f, 1.0f)) - st.latitude;
    float phi   = (2.0f * st.longitude * RandF(0.0f, 1.0f)) - st.longitude;

    // Initial velocity along +Z
    float speed = st.speed;
    XMFLOAT3 vel = {0, 0, speed};

    // Rotate by theta around Y (latitude spread)
    float sinT = sinf(theta), cosT = cosf(theta);
    vel.x = vel.z * sinT;
    vel.z = vel.z * cosT;

    // Rotate by phi around Z (longitude spread)
    float sinP = sinf(phi), cosP = cosf(phi);
    vel.y = vel.x * sinP;
    vel.x = vel.x * cosP;

    // Transform velocity to world space (direction only, w=0)
    XMVECTOR velV = XMVectorSet(vel.x, vel.y, vel.z, 0);
    XMVECTOR wVel = XMVector4Transform(velV, st.transform);
    // Engine: vel = WorldMatrixTransform(vel) - WorldMatrixTransform(origin)
    // This is equivalent to TransformNormal (w=0 transform)
    wVel = XMVectorSubtract(wVel, XMVectorSet(0, 0, 0, 0)); // no-op for w=0
    XMStoreFloat3(&p.velocity, wVel);

    p.lifeSpan = cfg.lifespan;
    p.initLife = cfg.lifespan;
    p.emitterId = 0; // will be set below
    p.childModelHandle = nextHandle++;

    // Sub-frame age randomization
    float subAge = dt * RandF(0.0f, 1.0f);
    if (subAge > 0 && subAge < p.lifeSpan) {
        float az = -(st.gravity);
        p.position.x += p.velocity.x * subAge;
        p.position.y += p.velocity.y * subAge;
        p.position.z += p.velocity.z * subAge + 0.5f * az * subAge * subAge;
        p.velocity.z += az * subAge;
        p.lifeSpan -= subAge;
    }

    // Find emitter ID
    for (auto& [eid, e] : emitters_) {
        if (&e == &em) { p.emitterId = eid; break; }
    }

    // Compute initial world transform
    XMMATRIX scale = XMMatrixScaling(cfg.scale, cfg.scale, cfg.scale);
    XMMATRIX trans = XMMatrixTranslation(p.position.x, p.position.y, p.position.z);
    PE1BirthEvent birth;
    birth.handle = p.childModelHandle;
    birth.emitterId = p.emitterId;
    birth.worldTransform = scale * trans;
    result.born.push_back(birth);

    em.particles.push_back(p);
}

float PE1System::RandF(float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng_);
}

} // namespace WhiteoutDex
