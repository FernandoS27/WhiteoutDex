#include "plane_emitter.h"
#include "../particle.h"   // legacy ParticleEmitterConfig (boundary type)

#include <cmath>

namespace WhiteoutDex::particle {

PlaneEmitter::PlaneEmitter() {
    type_ = EmitterType::Plane;
}

void ApplyInit(PlaneEmitter& e, const PlaneEmitterInit& init) {
    e.SetTextureDimensions(init.textureRows, init.textureCols);
    e.SetKey(0, init.keys[0]);
    e.SetKey(1, init.keys[1]);
    e.SetLifeSpan(init.lifeSpan);
    e.SetParticleStyle(init.hasHead, init.hasTail, init.tailLength);
    e.SetAngularVelocity(init.angularVelocity);
    e.SetSortZ(init.sortZ);
    e.SetUseModelSpace(init.modelSpace);
    e.SetXYQuads(init.xyQuads);
    e.SetPriorityPlane(init.priorityPlane);
    e.SetReplaceableId(init.replaceableId);
    e.SetMaterial(init.material);
    e.SetCoordSpace(init.coordSpace);
    e.SetLongitude(init.longitude);
    if (init.squirtAtStart) e.Squirt();
}

namespace {

FilterMode LegacyFilterToService(int legacy) {
    // Legacy FILTER_* constants from renderer/constants.h, mapped to the
    // service enum. Anything unexpected falls back to Blend.
    // FILTER_NONE=0, TRANSPARENT=1, BLEND=2, ADDITIVE=3, ADD_ALPHA=4,
    //   MODULATE=5, MODULATE_2X=6.
    switch (legacy) {
        case 1:  return FilterMode::AlphaKey;    // TRANSPARENT (alpha-test)
        case 2:  return FilterMode::Blend;
        case 3:  return FilterMode::Additive;
        case 4:  return FilterMode::Additive;    // ADD_ALPHA — same src/dst as Add
        case 5:  return FilterMode::Modulate;
        case 6:  return FilterMode::Modulate2X;
        default: return FilterMode::Blend;
    }
}

ImVector RgbFloatToImVector(const Vector3f& rgb, float alpha) {
    // Legacy ParticleEmitterConfig stores colour RGB in [0,1] and alpha in
    // [0, 255] as a float. Match MDX truncation semantics: (int)(255 * v).
    auto clamp8 = [](float v) -> uint8_t {
        if (v <= 0.0f) return 0;
        if (v >= 255.0f) return 255;
        return static_cast<uint8_t>(v);
    };
    return {
        clamp8(alpha),
        clamp8(rgb.x * 255.0f),
        clamp8(rgb.y * 255.0f),
        clamp8(rgb.z * 255.0f),
    };
}

} // namespace

PlaneEmitterInit InitFromLegacyConfig(const ParticleEmitterConfig& cfg) {
    PlaneEmitterInit init;

    init.textureRows = static_cast<uint32_t>(cfg.rows  > 0 ? cfg.rows  : 1);
    init.textureCols = static_cast<uint32_t>(cfg.cols  > 0 ? cfg.cols  : 1);
    init.lifeSpan    = cfg.lifeSpan;
    init.tailLength  = cfg.tailLength;

    // Particle type: legacy convention is 1=Head, 2=Tail, 3=Both (the
    // 3ds Max UI encoding, not the MDX binary's 0/1/2 — see
    // docs/PARTICLEEMITTERS2.md §1.2).
    init.hasHead = (cfg.particleType == 1 || cfg.particleType == 3);
    init.hasTail = (cfg.particleType == 2 || cfg.particleType == 3);

    init.sortZ      = cfg.sortZ;
    init.modelSpace = cfg.modelSpace;
    init.xyQuads    = cfg.xyQuad;

    // Longitude default per plan §3.8: 2π when LineEmitter clear, 0 when set.
    init.longitude = cfg.lineEmitter ? 0.0f : 6.2831853071795864769f;

    init.angularVelocity = 0.0f;    // not in the legacy config
    init.priorityPlane   = cfg.priorityPlane;
    init.replaceableId   = 0;       // not in the legacy config

    init.material.textureId     = cfg.textureId;
    init.material.filterMode    = LegacyFilterToService(cfg.filterMode);
    init.material.unshaded      = cfg.unshaded;
    init.material.unfogged      = cfg.unfogged;
    init.material.replaceableId = 0;

    init.squirtAtStart = cfg.squirt;

    // 2-key folding (plan §3.4). midTime × lifeSpan → keys[0].endTime;
    // middleColor/alpha/scale → end of key0 and start of key1.
    const ImVector startColor = RgbFloatToImVector(cfg.startColor, cfg.startAlpha);
    const ImVector midColor   = RgbFloatToImVector(cfg.midColor,   cfg.midAlpha);
    const ImVector endColor   = RgbFloatToImVector(cfg.endColor,   cfg.endAlpha);

    auto& k0 = init.keys[0];
    k0.endTime        = cfg.midTime * cfg.lifeSpan;
    k0.startColor     = startColor;
    k0.endColor       = midColor;
    k0.startScale     = cfg.startScale;
    k0.endScale       = cfg.midScale;
    k0.headCellStart  = cfg.headLifeStart;
    k0.headCellEnd    = cfg.headLifeEnd;
    k0.headCellRepeat = cfg.headLifeRepeat;
    k0.tailCellStart  = cfg.tailLifeStart;
    k0.tailCellEnd    = cfg.tailLifeEnd;
    k0.tailCellRepeat = cfg.tailLifeRepeat;

    auto& k1 = init.keys[1];
    k1.endTime        = cfg.lifeSpan;
    k1.startColor     = midColor;
    k1.endColor       = endColor;
    k1.startScale     = cfg.midScale;
    k1.endScale       = cfg.endScale;
    k1.headCellStart  = cfg.headDecayStart;
    k1.headCellEnd    = cfg.headDecayEnd;
    k1.headCellRepeat = cfg.headDecayRepeat;
    k1.tailCellStart  = cfg.tailDecayStart;
    k1.tailCellEnd    = cfg.tailDecayEnd;
    k1.tailCellRepeat = cfg.tailDecayRepeat;

    // Simulate in the renderer-native default. The caller (adapter) is
    // responsible for producing per-frame transforms in that same space, so
    // the particle service needs no conversion on the hot path.
    init.coordSpace = kDefaultCoordSpace;

    return init;
}

void PlaneEmitter::CreateParticle(Particle2& p, float elapsed) {
    // Sub-frame age. RE: p.m_age = elapsed * real_(). For burst spawns the
    // caller passes elapsed = 0 so age stays at 0.
    float r = CRandom::real_(randSeed_);
    p.keyFrame = 0;
    p.age = elapsed * r;

    // Spawn position on the width×height plane.
    // Two reals_ calls in RE order (height first, then width) — the VALUES
    // are swapped to axes versus the legacy port: "Length" (long axis) maps
    // to local +X, "Width" to local +Y. This matches MDX authoring where
    // an emitter's Length extends along the model-forward direction.
    float x = CRandom::reals_(randSeed_) * height_ * 0.5f;   // Length → local +X
    float y = CRandom::reals_(randSeed_) * width_  * 0.5f;   // Width  → local +Y

    Vector3f localPos{ x, y, 0.0f };
    if ((flags_ & kFlagUseModelSpace) != 0) {
        p.position = localPos;
    } else {
        p.position = whiteout::transform_point(localPos, modelToWorld_);
    }

    // Velocity: start along +Z, rotate by latitude around Y, then by
    // longitude around Z. Each rotation angle is reals_() * magnitude.
    float rotY = latitude_  * CRandom::reals_(randSeed_);
    float rotZ = longitude_ * CRandom::reals_(randSeed_);

    float speed = CalcVelocity();

    // After the Y rotation: (speed*sin(rotY), 0, speed*cos(rotY)).
    float vx = speed * std::sin(rotY);
    float vz = speed * std::cos(rotY);
    // After the Z rotation: y = vx*sin(rotZ), x = vx*cos(rotZ).
    float vy = vx * std::sin(rotZ);
    vx       = vx * std::cos(rotZ);

    Vector3f localVel{ vx, vy, vz };
    if ((flags_ & kFlagUseModelSpace) != 0) {
        p.velocity = localVel;
    } else {
        p.velocity = whiteout::transform_normal(localVel, modelToWorld_);
    }
}

} // namespace WhiteoutDex::particle
