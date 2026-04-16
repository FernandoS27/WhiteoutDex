#pragma once
// ============================================================================
// CPlaneParticleEmitter port — the only emitter type WC3 MDX uses.
//
// Extends Emitter2 with width/height spawn plane + lat/lon spread angles.
// See CPlaneParticleEmitter.cpp in BlizzPartRE/pseudocode for the spawn math.
//
// IMPORTANT: when coordSpace == CoordSpace::Blizzard (the default) the caller
// MUST pass a modelToWorld matrix that is itself in Blizzard space — i.e. the
// MDX node TRS tracks with the Blizzard→Max swiz NOT applied. The MDX adapter
// at [mdx_model_adapter.cpp:82] currently swizzles all node tracks to Max
// space; Phase 4 of the plan (docs/PARTICLEEMITTERS2.md §4) adds a skip-swiz
// path for PE2 nodes specifically. Until that lands, this emitter should be
// driven from a caller that feeds it Blizzard-space transforms.
// ============================================================================

#include "particle2_emitter.h"

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
    // Simulation coord-space (Blizzard for MDX-driven emitters).
    CoordSpace            coordSpace    = CoordSpace::Blizzard;
};

class PlaneEmitter;

// Apply the init payload to a freshly-constructed PlaneEmitter. Declared
// after the forward-decl so the class body below can reference `PlaneEmitter`
// and this free function can follow it.
void ApplyInit(PlaneEmitter& e, const PlaneEmitterInit& init);

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
