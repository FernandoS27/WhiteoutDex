#include "plane_emitter.h"

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

void PlaneEmitter::CreateParticle(Particle2& p, float elapsed) {
    // Sub-frame age. RE: p.m_age = elapsed * real_(). For burst spawns the
    // caller passes elapsed = 0 so age stays at 0.
    float r = CRandom::real_(randSeed_);
    p.keyFrame = 0;
    p.age = elapsed * r;

    // Spawn position on the width×height plane (Blizzard-space x, y).
    // RE order: y then x (CPlaneParticleEmitter.cpp:133–134).
    float y = CRandom::reals_(randSeed_) * height_ * 0.5f;
    float x = CRandom::reals_(randSeed_) * width_  * 0.5f;

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
