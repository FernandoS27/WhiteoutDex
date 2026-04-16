#pragma once
// ============================================================================
// ParticlePool — alive/dead index stacks backed by a growing Particle2 array.
//
// Mirrors the engine's TSGrowableArray<CParticle2> + CParticleStack m_alive +
// CParticleStack m_dead. Particles are referenced by index; the two stacks
// partition the index space.
//
// Sync(rate, lifespan) ensures capacity of ceilPow2(1.15 * rate * lifespan),
// matching SyncAllocation at CParticleEmitter2.cpp:381.
// ============================================================================

#include "particle2.h"
#include <cstdint>
#include <vector>

namespace WhiteoutDex::particle {

class ParticlePool {
public:
    void Sync(float emissionRate, float lifeSpan);
    void Clear();
    void Compact();     // release all storage (dice compaction path)

    // Index-space access
    Particle2&       operator[](size_t idx)       { return particles_[idx]; }
    const Particle2& operator[](size_t idx) const { return particles_[idx]; }

    // Alive stack
    size_t AliveCount() const { return alive_.size(); }
    uint32_t AliveAt(size_t i) const { return alive_[i]; }
    void RemoveAliveAt(size_t i);

    // Dead stack: spawn path pops, expiry pushes
    bool     DeadEmpty() const { return dead_.empty(); }
    uint32_t PopDead();
    void     PushDead(uint32_t idx);
    void     PushAlive(uint32_t idx) { alive_.push_back(idx); }

    size_t Capacity() const { return particles_.size(); }

private:
    std::vector<Particle2> particles_;
    std::vector<uint32_t>  alive_;
    std::vector<uint32_t>  dead_;
};

} // namespace WhiteoutDex::particle
