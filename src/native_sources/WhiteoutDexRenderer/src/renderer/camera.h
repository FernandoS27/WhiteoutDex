#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Orbital Camera
// Ported from Magos War3 Model Editor Camera.cpp
// ============================================================================

#include "types.h"
#include "coordinate_system.h"
#include <cmath>

namespace WhiteoutDex {

class Camera {
public:
    // Magos defaults from Constants.h
    // NOTE: Can't use DEFAULT_PITCH as name — wingdi.h #defines it as 0!
    static constexpr float kDefaultPitch    = 0.3f;           // ~17° slight top-down
    // Default yaw places the camera on the model's front side. Max convention
    // puts the "front" direction at -Y (yaw = -PI/2 ⇒ camera at -Y); we derive
    // the default-space equivalent by transforming that axis through the
    // renderer-native coord system. All currently-supported spaces are Z-up
    // so pitch is invariant.
    static inline const float kDefaultYaw =
        []{
            Vector3f frontMax{0.0f, -1.0f, 0.0f};
            Vector3f frontDefault =
                CoordinateSystem::ConvertDirection(CoordSpace::Max,
                                                   CoordinateSystem::Default(),
                                                   frontMax);
            // `source = target + distance * (cos(pitch)*cos(yaw), cos(pitch)*sin(yaw), ...)`
            // so (source - target) in the XY plane points along +frontDefault.
            return std::atan2(frontDefault.y, frontDefault.x);
        }();
    static constexpr float kDefaultDistance = 350.0f;
    static constexpr float kMinPitch        = -1.5607963f;   // -(PI/2) + 0.01
    static constexpr float kMaxPitch        =  1.5607963f;   // (PI/2) - 0.01
    static constexpr float kMinDistance     = 15.0f;
    static constexpr float kMaxDistance     = 8000.0f;
    static constexpr float kFactorPitch     = 0.02f;
    static constexpr float kFactorYaw       = 0.02f;
    static constexpr float kFactorDistance  = 0.002f;
    static constexpr float kFactorMove      = 0.004f;
    static constexpr float kFactorRelDist   = 500.0f;
    static constexpr float kFactorRelMove   = 500.0f;

    Camera() { Reset(); }

    void Reset() {
        pitch_    = kDefaultPitch;
        yaw_      = kDefaultYaw;
        distance_ = kDefaultDistance;
        target_   = Vector3f(0.f, 0.f, 50.f);  // center on model midpoint
    }

    void SetFromModel(float boundsRadius) {
        distance_ = (std::max)(boundsRadius * 2.0f, kDefaultDistance);
        distance_ = std::clamp(distance_, kMinDistance, kMaxDistance);
    }

    // Mouse input (same as Magos)
    void Rotate(int dx, int dy) {
        yaw_   -= dx * kFactorYaw;
        pitch_ += dy * kFactorPitch;
        pitch_  = std::clamp(pitch_, kMinPitch, kMaxPitch);
    }

    void Pan(int dx, int dy) {
        // Speed proportional to distance — intuitive at any zoom level
        float speed = distance_ * 0.003f;
        float cosP = cosf(pitch_), sinP = sinf(pitch_);
        float cosY = cosf(yaw_),   sinY = sinf(yaw_);

        // Right vector (in XY plane)
        float rx = -sinY, ry = cosY;
        // Up vector
        float ux = -sinP * cosY, uy = -sinP * sinY, uz = cosP;

        float mx = dx * speed;
        float my = dy * speed;

        target_.x += rx * mx + ux * my;
        target_.y += ry * mx + uy * my;
        target_.z +=            uz * my;
    }

    void Zoom(int delta) {
        float factor = distance_ / kFactorRelDist;
        distance_ -= delta * factor;
        distance_  = std::clamp(distance_, kMinDistance, kMaxDistance);
    }

    void ZoomSmooth(float amount) {
        distance_ -= amount;
        distance_  = std::clamp(distance_, kMinDistance, kMaxDistance);
    }

    // Camera vectors (Magos coordinate system: Z-up, right-handed)
    Vector3f GetSource() const {
        float cosP = cosf(pitch_);
        return Vector3f(
            target_.x + distance_ * cosP * cosf(yaw_),
            target_.y + distance_ * cosP * sinf(yaw_),
            target_.z + distance_ * sinf(pitch_)
        );
    }

    Vector3f GetTarget() const { return target_; }
    Vector3f GetUp()     const { return Vector3f(0.f, 0.f, 1.f); }

    float GetPitch()    const { return pitch_; }
    float GetYaw()      const { return yaw_; }
    float GetDistance()  const { return distance_; }

    void SetPitch(float p)    { pitch_ = std::clamp(p, kMinPitch, kMaxPitch); }
    void SetYaw(float y)      { yaw_ = y; }
    void SetDistance(float d)  { distance_ = std::clamp(d, kMinDistance, kMaxDistance); }
    void SetTarget(float x, float y, float z) { target_ = {x, y, z}; }

    // Build view matrix (right-handed, Z-up)
    Matrix44f GetViewMatrix() const {
        return Matrix44f::look_at_rh(GetSource(), GetTarget(), GetUp());
    }

private:
    float pitch_;
    float yaw_;
    float distance_;
    Vector3f target_;
};

} // namespace WhiteoutDex
