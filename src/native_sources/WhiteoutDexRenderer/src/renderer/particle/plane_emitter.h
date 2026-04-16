#pragma once
// ============================================================================
// CPlaneParticleEmitter port — the only emitter type WC3 MDX uses.
//
// Extends Emitter2 with width/height spawn plane + lat/lon spread angles.
// See CPlaneParticleEmitter.cpp in BlizzPartRE/pseudocode for the spawn math.
//
// Coord-space: MDX-driven emitters default to `CoordSpace::Max` (matches what
// the adapter's TransformMdxModelToMaxCoords already produced and the legacy
// renderer consumed). `CoordSpace::Blizzard` remains wired for a future
// accuracy pass; see docs/PARTICLEEMITTERS2.md §3.8 for the retrospective.
// ============================================================================

#include "particle2_emitter.h"

// Forward-declare the legacy config struct that still crosses the RenderService
// public boundary, so InitFromLegacyConfig can take it without forcing every
// user of plane_emitter.h to include particle.h.
namespace WhiteoutDex { struct ParticleEmitterConfig; }

namespace WhiteoutDex::particle {

// Static-configuration payload for a PlaneEmitter. Everything here is set
// once at registration time; per-frame values (emissionRate, speed, width,
// etc.) go through the regular setters during Simulate.
struct PlaneEmitterInit {
    // Sprite sheet
    uint32_t              textureRows   = 1;
    uint32_t              textureCols   = 1;
    // Keyframe table (life / decay)
    ParticleKey           keys[2];
    // Lifetime + shape
    float                 lifeSpan      = 0.0f;
    float                 tailLength    = 1.0f;
    float                 angularVelocity = 0.0f;     // not in MDX; zero unless set
    bool                  hasHead       = true;
    bool                  hasTail       = false;
    // Flags
    bool                  sortZ         = false;
    bool                  modelSpace    = false;
    bool                  xyQuads       = false;
    // Longitude default: 2π (full-circle spread). Loader sets 0 when the MDX
    // LineEmitter flag is set.
    float                 longitude     = 6.2831853071795864769f;
    // MDX squirts field → arm the NeedSquirt latch once at activation.
    bool                  squirtAtStart = false;
    // Identifying metadata
    int                   priorityPlane = 0;
    int                   replaceableId = 0;
    ParticleMaterialDesc  material;
    // Simulation coord-space. MDX-sourced emitters (via GetPlaneEmitterInits
    // or InitFromLegacyConfig) set this to CoordSpace::Max.
    CoordSpace            coordSpace    = CoordSpace::Max;
};

class PlaneEmitter;

// Apply the init payload to a freshly-constructed PlaneEmitter. Declared
// after the forward-decl so the class body below can reference `PlaneEmitter`.
void ApplyInit(PlaneEmitter& e, const PlaneEmitterInit& init);

// Translate a legacy ParticleEmitterConfig (the boundary type that callers
// still pass to RenderService::AddModel / LoadModel) into a PlaneEmitterInit.
// Performs the same 3-segment → 2-key folding, ParticleType unpacking and
// LineEmitter → longitude default mapping as the MDX adapter's
// GetPlaneEmitterInits, so both intake paths converge on identical
// PlaneEmitter state.
PlaneEmitterInit InitFromLegacyConfig(const ParticleEmitterConfig& cfg);

class PlaneEmitter : public Emitter2 {
public:
    PlaneEmitter();

    // Plane-specific setters
    void SetWidth(float w)     { width_ = w; }
    void SetHeight(float h)    { height_ = h; }
    void SetLatitude(float l)  { latitude_ = l; }
    void SetLongitude(float l) { longitude_ = l; }

    float Width() const        { return width_; }
    float Height() const       { return height_; }
    float Latitude() const     { return latitude_; }
    float Longitude() const    { return longitude_; }

protected:
    void CreateParticle(Particle2& p, float elapsed) override;

private:
    // +0x318, +0x31C, +0x320, +0x324 in RE.
    float width_     = 0.0f;
    float height_    = 0.0f;
    float latitude_  = 0.0f;   // radians
    float longitude_ = 0.0f;   // radians; 2π when LineEmitter clear, 0 when set
};

} // namespace WhiteoutDex::particle
