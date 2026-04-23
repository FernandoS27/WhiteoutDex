#pragma once
// ============================================================================
// CParticleEmitter2 port — the base quad-particle emitter.
//
// Subclasses override CreateParticle() (PlaneEmitter provides the width/height/
// lat/lon behaviour used by WC3). InternalUpdate / MoveParticle / the per-frame
// lifecycle live in the base class. See docs/PARTICLEEMITTERS2.md §2–§3.
//
// Flag bit layout (packed 12-bit m_flags at +0x2E4 in RE):
//   0   Visible         — cleared at end of every InternalUpdate
//   1   Enabled2        — static config gate
//   2   HasHead         — head quad present
//   3   HasTail         — tail quad present
//   4   SortZ           — depth-sort this emitter
//   5   NeedSquirt      — one-shot burst latch
//   6   Updated         — anim parent ran us this frame (public Update clears)
//   7   Paused          — anim parent set dt==0; cleared each public Update
//   8   SystemDead      — stops emission, lets trailing particles age out
//   9   UseModelSpace   — static config
//   10  XYQuads         — static config
//   11  UpdatedByAnim   — latched after first successful anim step
// ============================================================================

#include "particle2.h"
#include "particle_key.h"
#include "particle_material.h"
#include "particle_pool.h"
#include "rnd_seed.h"
#include "../coordinate_system.h"
#include "types.h"

#include <cstdint>

namespace WhiteoutDex::particle {

// Engine-wide emission multiplier (ParticleSystemManager::GetScaler in RE).
// InternalUpdate multiplies both burst and steady paths by this value.
void  SetGlobalEmissionScaler(float s);
float GetGlobalEmissionScaler();

enum EmitterFlag : uint32_t {
    kFlagVisible        = 0x001,
    kFlagEnabled2       = 0x002,
    kFlagHasHead        = 0x004,
    kFlagHasTail        = 0x008,
    kFlagSortZ          = 0x010,
    kFlagNeedSquirt     = 0x020,
    kFlagUpdated        = 0x040,
    kFlagPaused         = 0x080,
    kFlagSystemDead     = 0x100,
    kFlagUseModelSpace  = 0x200,
    kFlagXYQuads        = 0x400,
    kFlagUpdatedByAnim  = 0x800
};

enum class EmitterType : uint32_t {
    Base  = 0,   // PET_BASE_EMITTER
    Plane = 1    // PET_PLANE_EMITTER
};

class Emitter2 {
public:
    Emitter2();
    virtual ~Emitter2() = default;

    // --- Public control surface (mirrors CParticleEmitter2 setters) -----

    void SetVisible(bool v)             { SetFlag(kFlagVisible, v); }
    void SetEnabled2(bool v)            { SetFlag(kFlagEnabled2, v); }
    void SetSortZ(bool v)               { SetFlag(kFlagSortZ, v); }
    void SetUseModelSpace(bool v)       { SetFlag(kFlagUseModelSpace, v); }
    void SetXYQuads(bool v)             { SetFlag(kFlagXYQuads, v); }
    void SetSquirtPending(bool v)       { SetFlag(kFlagNeedSquirt, v); }

    void SetEmissionRate(float v)       { emissionRate_ = v; }
    void SetLifeSpan(float v)           { lifeSpan_ = v; }
    void SetVelocity(float v)           { velocity_ = v; }
    void SetAcceleration(float v)       { acceleration_ = v; }
    void SetVelocityVariation(float v)  { velocityVariation_ = v; }
    void SetAngularVelocity(float v)    { angularVelocity_ = v; }
    void SetTailLength(float v)         { tailLength_ = v; }

    void SetParticleStyle(bool hasHead, bool hasTail, float tailLength);
    void SetTextureDimensions(uint32_t rows, uint32_t cols);
    void SetMaterial(const ParticleMaterialDesc& d) { material_ = d; }
    void SetKey(int index, const ParticleKey& k);
    void SetPriorityPlane(int p)        { priorityPlane_ = p; }
    void SetReplaceableId(int id)       { replaceableId_ = id; }
    void SetCoordSpace(CoordSpace s)    { coordSpace_ = s; }

    void Squirt()                       { flags_ |= kFlagNeedSquirt; }
    void SetDead()                      { flags_ |= kFlagSystemDead; }
    void Flush();                       // kill all alive particles

    // Out-of-band world-matrix setter for callers that drive sim via the
    // non-anim Update(dt) path (e.g. a test harness, or a service loop that
    // refreshes transforms before Simulate).
    void SetModelToWorld(const Matrix44f& m) { modelToWorld_ = m; }

    // --- Per-frame tick -------------------------------------------------

    // Public entry from a non-animation driver. Runs InternalUpdate if this
    // frame hasn't already been stepped by an animation parent.
    void Update(float elapsed);

    // Animation parent entry. Stamps the world matrix, sets Updated flag,
    // runs InternalUpdate, and latches UpdatedByAnim on first successful step.
    void Update(float elapsed, const Matrix44f& worldMatrix);

    // --- Queries (read-only) --------------------------------------------

    bool Enabled() const                { return (flags_ & (kFlagVisible | kFlagEnabled2)) == (kFlagVisible | kFlagEnabled2); }
    bool IsDead() const                 { return (flags_ & kFlagSystemDead) != 0; }
    bool Visible() const                { return (flags_ & kFlagVisible) != 0; }
    bool HasHead() const                { return (flags_ & kFlagHasHead) != 0; }
    bool HasTail() const                { return (flags_ & kFlagHasTail) != 0; }
    bool SortZ() const                  { return (flags_ & kFlagSortZ) != 0; }
    bool UseModelSpace() const          { return (flags_ & kFlagUseModelSpace) != 0; }
    bool XYQuads() const                { return (flags_ & kFlagXYQuads) != 0; }
    uint32_t Flags() const              { return flags_; }

    EmitterType Type() const            { return type_; }
    CoordSpace  GetCoordSpace() const   { return coordSpace_; }

    float LifeSpan() const              { return lifeSpan_; }
    float EmissionRate() const          { return emissionRate_; }
    float Velocity() const              { return velocity_; }
    float Acceleration() const          { return acceleration_; }
    float VelocityVariation() const     { return velocityVariation_; }
    float AngularVelocity() const       { return angularVelocity_; }
    float TailLength() const            { return tailLength_; }
    int   PriorityPlane() const         { return priorityPlane_; }
    int   ReplaceableId() const         { return replaceableId_; }

    uint32_t TextureRows() const        { return textureRows_; }
    uint32_t TextureCols() const        { return textureCols_; }
    uint32_t TextureLog() const         { return textureLog_; }
    float    OoTextureWidth() const     { return ooTextureWidth_; }
    float    OoTextureHeight() const    { return ooTextureHeight_; }

    const ParticleKey& Key(int i) const { return keys_[i]; }
    const ParticleMaterialDesc& Material() const { return material_; }
    const Matrix44f& ModelToWorld() const { return modelToWorld_; }

    const ParticlePool& Pool() const    { return pool_; }
    ParticlePool&       Pool()          { return pool_; }

    int TotalAlive() const              { return static_cast<int>(pool_.AliveCount()); }

protected:
    // --- Spawn hook: subclasses must override -----------------------
    // elapsed is the current frame dt; pass 0.0f for burst/squirt spawns.
    virtual void CreateParticle(Particle2& p, float elapsed) = 0;

    // Velocity randomization: v * (1 + reals_() * variation).
    float CalcVelocity();

    // --- Shared helpers ---------------------------------------------
    void SetFlag(uint32_t mask, bool on) {
        if (on) flags_ |= mask; else flags_ &= ~mask;
    }

    // Physics step: Euler with z-axis acceleration.
    void MoveParticle(Particle2& p, float elapsed) const;

    // Main lifecycle loop.
    void InternalUpdate(float elapsed);

    // Capacity sync.
    void Sync() { pool_.Sync(emissionRate_, lifeSpan_); }

protected:
    EmitterType type_                    = EmitterType::Base;

    // Flags and material
    uint32_t flags_                      = kFlagEnabled2 | kFlagHasHead; // RE ctor default
    CoordSpace coordSpace_               = kDefaultCoordSpace;

    // Scalars (all lifted from +0x100..+0x1C0 region of CParticleEmitter2)
    float emissionRate_                  = 0.0f;
    float lifeSpan_                      = 0.0f;
    float tailLength_                    = 1.0f;
    float velocity_                      = 0.0f;
    float acceleration_                  = 0.0f;
    float velocityVariation_             = 0.1f;   // RE default
    float angularVelocity_               = 0.0f;

    // Fractional emission accumulator (m_numNew in RE).
    float numNew_                        = 0.0f;

    // Texture sheet
    uint32_t textureRows_                = 1;
    uint32_t textureCols_                = 1;
    uint32_t textureLog_                 = 0;
    float    ooTextureWidth_             = 1.0f;
    float    ooTextureHeight_            = 1.0f;

    // Keys: exactly two
    ParticleKey keys_[2];

    // Identifying / sorting metadata
    int replaceableId_                   = 0;
    int priorityPlane_                   = 0;

    // Material descriptor
    ParticleMaterialDesc material_;

    // Transform from anim parent
    Matrix44f modelToWorld_              = Matrix44f::identity();

    // RNG (per-emitter)
    RndSeed randSeed_;

    // Particle pool
    ParticlePool pool_;
};

} // namespace WhiteoutDex::particle
