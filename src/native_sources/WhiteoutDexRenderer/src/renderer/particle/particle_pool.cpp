#include "particle_pool.h"

namespace WhiteoutDex::particle {

namespace {

// Round up to next power of 2. Matches the SyncAllocation loop at
// CParticleEmitter2.cpp:394 (strip-set-bits).
uint32_t CeilPow2(uint32_t x) {
    if (x == 0) return 1;
    if ((x & (x - 1)) == 0) return x;   // already pow2
    uint32_t v = x - 1;
    v |= v >> 1;  v |= v >> 2;
    v |= v >> 4;  v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

} // namespace

void ParticlePool::Sync(float emissionRate, float lifeSpan) {
    if (emissionRate <= 0.0f || lifeSpan <= 0.0f) return;

    uint32_t arraySize = static_cast<uint32_t>(1.15f * emissionRate * lifeSpan);
    if (arraySize == 0) return;
    uint32_t oldSize = static_cast<uint32_t>(particles_.size());
    if (oldSize >= arraySize) return;

    // Reserve rounded-up capacity; live size stays at arraySize per RE.
    uint32_t reserve = CeilPow2(arraySize);
    particles_.reserve(reserve);
    alive_.reserve(reserve);
    dead_.reserve(reserve);

    particles_.resize(arraySize);
    // Newly-grown slots go on the dead stack as available-for-spawn.
    for (uint32_t u = oldSize; u < arraySize; ++u) {
        dead_.push_back(u);
    }
}

void ParticlePool::Clear() {
    alive_.clear();
    dead_.clear();
    // Mark every slot as available.
    dead_.reserve(particles_.size());
    for (uint32_t i = 0; i < particles_.size(); ++i) {
        dead_.push_back(i);
    }
}

void ParticlePool::Compact() {
    particles_.clear();
    particles_.shrink_to_fit();
    alive_.clear();
    alive_.shrink_to_fit();
    dead_.clear();
    dead_.shrink_to_fit();
}

void ParticlePool::RemoveAliveAt(size_t i) {
    // Swap-remove; ordering within the alive stack doesn't carry meaning.
    if (i + 1 < alive_.size()) {
        alive_[i] = alive_.back();
    }
    alive_.pop_back();
}

uint32_t ParticlePool::PopDead() {
    uint32_t idx = dead_.back();
    dead_.pop_back();
    return idx;
}

void ParticlePool::PushDead(uint32_t idx) {
    dead_.push_back(idx);
}

} // namespace WhiteoutDex::particle
