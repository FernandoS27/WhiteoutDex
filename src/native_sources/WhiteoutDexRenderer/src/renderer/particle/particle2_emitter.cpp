#include "particle2_emitter.h"

#include <algorithm>
#include <cmath>

namespace WhiteoutDex::particle {

namespace {

// Monotonic counter for per-emitter seed diversification. Mirrors the RE's
// `(rand() << 16) | rand()` but without libc's rand state.
std::atomic<uint32_t> g_seedCounter{1};

// Engine dt clamp (see CParticleEmitter2.cpp:942).
constexpr float kMaxDt = 0.5f;

// Global emission scaler. ParticleService owns the setter; this is the
// storage referenced by InternalUpdate.
float g_globalScaler = 1.0f;

} // namespace

void SetGlobalEmissionScaler(float s) { g_globalScaler = s; }
float GetGlobalEmissionScaler() { return g_globalScaler; }

Emitter2::Emitter2() {
    // Fresh RNG seed per emitter. Two copies get different sequences
    // (matches RE copy-ctor behaviour).
    uint32_t counter = g_seedCounter.fetch_add(1, std::memory_order_relaxed);
    randSeed_.SetSeed(MakeSeedFromTime(counter));
}

// ---------------------------------------------------------------------------
// Setters requiring logic
// ---------------------------------------------------------------------------

void Emitter2::SetParticleStyle(bool hasHead, bool hasTail, float tailLength) {
    SetFlag(kFlagHasHead, hasHead);
    SetFlag(kFlagHasTail, hasTail);
    tailLength_ = tailLength;
}

void Emitter2::SetTextureDimensions(uint32_t rows, uint32_t cols) {
    // RE requires power-of-2. We permit non-pow-2 but silently ignore the
    // bitmask/shift fast path (fall back to modulo/divide).
    textureRows_ = (rows > 0) ? rows : 1;
    textureCols_ = (cols > 0) ? cols : 1;

    ooTextureWidth_  = 1.0f / static_cast<float>(textureCols_);
    ooTextureHeight_ = 1.0f / static_cast<float>(textureRows_);

    textureLog_ = 0;
    uint32_t c = textureCols_;
    // log2 for power-of-2 columns; for non-pow-2 this yields floor(log2)
    // which the bitmask fast-path won't use anyway.
    while (c > 1) { c >>= 1; ++textureLog_; }
}

void Emitter2::SetKey(int index, const ParticleKey& k) {
    if (index == 0 || index == 1) {
        keys_[index] = k;
    }
}

void Emitter2::Flush() {
    // Kill everything alive. We don't call a virtual DestroyParticle hook
    // (RE has one but it's empty for all concrete types).
    while (pool_.AliveCount() > 0) {
        size_t last = pool_.AliveCount() - 1;
        uint32_t idx = pool_.AliveAt(last);
        pool_.RemoveAliveAt(last);
        pool_.PushDead(idx);
    }
}

// ---------------------------------------------------------------------------
// Per-emitter RNG sampling
// ---------------------------------------------------------------------------

float Emitter2::CalcVelocity() {
    float r = CRandom::reals_(randSeed_);
    return velocity_ * (1.0f + r * velocityVariation_);
}

// ---------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------

void Emitter2::MoveParticle(Particle2& p, float elapsed) const {
    // a = (0, 0, -acceleration) in emitter-local frame.
    const float az = -acceleration_;
    p.position.x += p.velocity.x * elapsed;
    p.position.y += p.velocity.y * elapsed;
    p.position.z += p.velocity.z * elapsed + 0.5f * az * elapsed * elapsed;
    p.velocity.z += az * elapsed;
}

// ---------------------------------------------------------------------------
// Main update loop (ports CParticleEmitter2::InternalUpdate)
// ---------------------------------------------------------------------------

void Emitter2::InternalUpdate(float elapsed) {
    // Clamp per RE.
    if (elapsed < 0.0f) elapsed = 0.0f;
    if (elapsed > kMaxDt) elapsed = kMaxDt;

    const bool squirtPending = (flags_ & kFlagNeedSquirt) != 0;
    const bool paused        = (flags_ & kFlagPaused) != 0;
    const bool dead          = (flags_ & kFlagSystemDead) != 0;
    const bool enabled       = Enabled();

    // Sync allocation if enabled or about to burst.
    if (enabled || squirtPending) {
        Sync();
    }

    // --- Burst path ---
    if (squirtPending && !paused && !dead) {
        int numToEmit = static_cast<int>(emissionRate_ * g_globalScaler);
        while (numToEmit > 0 && !pool_.DeadEmpty()) {
            uint32_t idx = pool_.PopDead();
            pool_.PushAlive(idx);
            CreateParticle(pool_[idx], 0.0f);   // age = 0 for burst
            --numToEmit;
        }
        flags_ &= ~kFlagNeedSquirt;
    }

    // --- Steady emission path ---
    if (enabled && !paused && !dead) {
        numNew_ += elapsed * emissionRate_ * g_globalScaler;
        uint32_t planned = static_cast<uint32_t>(numNew_);
        uint32_t emitted = 0;
        while (planned > 0 && !pool_.DeadEmpty()) {
            uint32_t idx = pool_.PopDead();
            pool_.PushAlive(idx);
            CreateParticle(pool_[idx], elapsed);
            ++emitted;
            --planned;
        }
        numNew_ -= static_cast<float>(emitted);
    }

    // --- Per-particle update ---
    // Iterate by index because expiry compacts the alive stack.
    for (size_t i = 0; i < pool_.AliveCount(); ) {
        uint32_t idx = pool_.AliveAt(i);
        Particle2& p = pool_[idx];

        p.age += elapsed;

        // Advance keyframe(s).
        while (p.keyFrame < 2 && p.age > keys_[p.keyFrame].endTime) {
            ++p.keyFrame;
        }

        if (lifeSpan_ <= p.age || p.keyFrame >= 2) {
            pool_.PushDead(idx);
            pool_.RemoveAliveAt(i);
            // i stays; swap-remove put the previously-last alive particle here.
        } else {
            MoveParticle(p, elapsed);
            ++i;
        }
    }

    // Compaction: 1-in-32 chance to drop storage if the alive pool is empty.
    if (pool_.AliveCount() == 0 && CRandom::dice_(32, g_globalRnd) == 0) {
        pool_.Compact();
    }

    // Visible is a single-frame latch cleared at the end.
    flags_ &= ~kFlagVisible;
}

// ---------------------------------------------------------------------------
// Public tick entries
// ---------------------------------------------------------------------------

void Emitter2::Update(float elapsed) {
    if ((flags_ & kFlagUpdated) == 0) {
        InternalUpdate(elapsed);
    }
    flags_ &= ~kFlagUpdated;
    flags_ &= ~kFlagPaused;
}

void Emitter2::Update(float elapsed, const Matrix44f& worldMatrix) {
    modelToWorld_ = worldMatrix;

    if (elapsed == 0.0f) {
        flags_ |= kFlagPaused;
        return;
    }

    flags_ |= kFlagUpdated;
    InternalUpdate(elapsed);

    if ((flags_ & kFlagUpdatedByAnim) == 0) {
        flags_ |= kFlagUpdatedByAnim;
        // Service owns the on-deck -> active transition; we just raise the
        // latch here. The service's tick loop checks this flag if it needs to
        // know whether an emitter has ever been anim-driven.
    }
}

} // namespace WhiteoutDex::particle
