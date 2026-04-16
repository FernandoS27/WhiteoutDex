#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — PE2 boundary types.
//
// The legacy ParticleSystem class has been removed. What remains here are the
// two structs that cross the RenderService public API:
//
//   ParticleEmitterConfig — static emitter configuration produced by MDX /
//                           Max-scene adapters and passed to AddModel /
//                           LoadModel.
//   ParticleEmitterState  — per-frame animated state pushed by adapters via
//                           FrameState::particleStates and consumed in
//                           ApplyParticleFrameStates.
//
// Both types are translated internally to the PE2 service's PlaneEmitterInit /
// PlaneEmitter setters in renderer/particle/. See docs/PARTICLEEMITTERS2.md.
// ============================================================================

#include "types.h"

namespace WhiteoutDex {

// Static emitter configuration (sent once from MDX or Max importer).
struct ParticleEmitterConfig {
    int   textureId    = -1;
    int   filterMode   = 0;      // 0=None,1=Transparent,2=Blend,3=Additive,4=AddAlpha,5=Modulate,6=Modulate2x
    int   rows = 1, cols = 1;
    bool  unshaded     = false;

    float lifeSpan     = 1.0f;
    bool  squirt       = false;

    // 3-segment colour/alpha/scale. Colour channels are normalised [0,1]
    // floats; alpha is stored as a float in [0,255] to match byte-accurate
    // import. The renderer folds these into two CParticleKey objects at
    // registration time (plan §3.4).
    Vector3f startColor  = {1,1,1};
    Vector3f midColor    = {0.5f,0.5f,0.5f};
    Vector3f endColor    = {0,0,0};
    float startAlpha = 255, midAlpha = 128, endAlpha = 0;
    float startScale = 10, midScale = 10, endScale = 10;
    float midTime    = 0.5f;

    // Head/tail
    int   particleType = 1;   // 3ds Max UI convention: 1=Head, 2=Tail, 3=Both
    float tailLength   = 1.0f;

    // UV animation frames (life / decay × head / tail).
    int headLifeStart=0, headLifeEnd=0, headLifeRepeat=1;
    int headDecayStart=0, headDecayEnd=0, headDecayRepeat=1;
    int tailLifeStart=0, tailLifeEnd=0, tailLifeRepeat=1;
    int tailDecayStart=0, tailDecayEnd=0, tailDecayRepeat=1;

    // Flags
    bool modelSpace  = false;
    bool xyQuad      = false;
    bool sortZ       = false;
    bool lineEmitter = false;
    bool unfogged    = false;

    // Misc
    int  count         = 0;   // legacy cap (unused by the service)
    int  priorityPlane = 0;
};

// Per-frame emitter state (animatable values + transform).
struct ParticleEmitterState {
    Matrix44f transform = Matrix44f::identity();
    float emissionRate = 0;
    float speed        = 0;
    float variation    = 0;
    float coneAngle    = 0;    // latitude, radians
    float gravity      = 0;
    float width        = 0;
    float length       = 0;
    float visibility   = 1.0f;
};

} // namespace WhiteoutDex
