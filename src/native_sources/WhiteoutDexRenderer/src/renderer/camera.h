#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Camera
//
// Mirrors Previewd's CCamera::SetupWorldProjection (0x140338420). HD /
// SD_on_HD draws consume the LH pair; SD Path A + UI overlays use RH.
// Orbital mode = free camera. Direct mode = MDX preset with absolute
// position/target/roll/fov.
// ============================================================================

#include "types.h"
#include "coordinate_system.h"
#include <algorithm>
#include <cmath>

namespace WhiteoutDex {

// View and projection must match handedness, so build them together.
struct CameraMatrices {
    Matrix44f viewLH, viewRH;
    Matrix44f projLH, projRH;
};

class Camera {
public:
    enum class Mode {
        Orbital,  // pitch/yaw/distance around target; mouse-driven.
        Direct,   // position/target set absolutely (MDX preset).
    };

    // ---- Orbital defaults (Magos-derived) ----
    static constexpr float kDefaultPitch    = 0.3f;
    static inline const float kDefaultYaw =
        []{
            Vector3f frontMax{0.0f, -1.0f, 0.0f};
            Vector3f frontDefault =
                CoordinateSystem::ConvertDirection(CoordSpace::Max,
                                                   CoordinateSystem::Default(),
                                                   frontMax);
            return std::atan2(frontDefault.y, frontDefault.x);
        }();
    static constexpr float kDefaultDistance = 350.0f;
    static constexpr float kMinPitch        = -1.5607963f;
    static constexpr float kMaxPitch        =  1.5607963f;
    static constexpr float kMinDistance     = 15.0f;
    static constexpr float kMaxDistance     = 8000.0f;
    static constexpr float kFactorPitch     = 0.02f;
    static constexpr float kFactorYaw       = 0.02f;
    static constexpr float kFactorDistance  = 0.002f;
    static constexpr float kFactorMove      = 0.004f;
    static constexpr float kFactorRelDist   = 500.0f;
    static constexpr float kFactorRelMove   = 500.0f;

    // Diagonal FOV ≈ 1.3 rad matches the old π/4 vertical framing at
    // ~1.33 aspect after the `1/sqrt(aspect²+1)` compensation.
    static constexpr float kDefaultFovDiagonal = 1.30f;
    static constexpr float kDefaultNearZ       = 1.0f;
    static constexpr float kDefaultFarZ        = 10000.0f;

    Camera() { Reset(); }

    void Reset() {
        mode_     = Mode::Orbital;
        pitch_    = kDefaultPitch;
        yaw_      = kDefaultYaw;
        distance_ = kDefaultDistance;
        target_   = {0.f, 0.f, 50.f};
        roll_     = 0.0f;
        localYaw_ = localPitch_ = localRoll_ = 0.0f;
        fovDiagonal_ = kDefaultFovDiagonal;
        zNear_ = kDefaultNearZ;
        zFar_  = kDefaultFarZ;
        directPosition_ = {0.f, 0.f, 0.f};
        directTarget_   = {0.f, 0.f, 0.f};
    }

    void SetFromModel(float boundsRadius) {
        distance_ = (std::max)(boundsRadius * 2.0f, kDefaultDistance);
        distance_ = std::clamp(distance_, kMinDistance, kMaxDistance);
    }

    // ---- Mouse input (orbital only; a mouse gesture while in Direct
    // mode does not implicitly switch modes — callers decide). ----
    void Rotate(int dx, int dy) {
        yaw_   -= dx * kFactorYaw;
        pitch_ += dy * kFactorPitch;
        pitch_  = std::clamp(pitch_, kMinPitch, kMaxPitch);
    }
    void Pan(int dx, int dy) {
        float speed = distance_ * 0.003f;
        float cosP = cosf(pitch_), sinP = sinf(pitch_);
        float cosY = cosf(yaw_),   sinY = sinf(yaw_);
        float rx = -sinY,               ry =  cosY;
        float ux = -sinP * cosY,        uy = -sinP * sinY, uz = cosP;
        float mx = dx * speed, my = dy * speed;
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

    // ---- Orbital accessors ----
    float GetPitch()    const { return pitch_; }
    float GetYaw()      const { return yaw_; }
    float GetDistance() const { return distance_; }
    Vector3f GetTarget() const {
        return (mode_ == Mode::Direct) ? directTarget_ : target_;
    }
    Vector3f GetSource() const {
        if (mode_ == Mode::Direct) return directPosition_;
        float cosP = cosf(pitch_);
        return Vector3f(
            target_.x + distance_ * cosP * cosf(yaw_),
            target_.y + distance_ * cosP * sinf(yaw_),
            target_.z + distance_ * sinf(pitch_)
        );
    }

    void SetPitch(float p)    { pitch_ = std::clamp(p, kMinPitch, kMaxPitch); }
    void SetYaw(float y)      { yaw_ = y; }
    void SetDistance(float d) { distance_ = std::clamp(d, kMinDistance, kMaxDistance); }
    void SetTarget(float x, float y, float z) { target_ = {x, y, z}; }
    void SetTarget(const Vector3f& t) { target_ = t; }

    // ---- Projection knobs ----
    float GetFovDiagonal() const { return fovDiagonal_; }
    float GetNearZ()       const { return zNear_; }
    float GetFarZ()        const { return zFar_; }
    void  SetFovDiagonal(float rad) { fovDiagonal_ = rad; }
    void  SetClip(float nearZ, float farZ) { zNear_ = nearZ; zFar_ = farZ; }

    // ---- Roll (rotates the up vector around the forward axis) ----
    float GetRoll() const { return roll_; }
    void  SetRoll(float r) { roll_ = r; }

    // ---- Local post-rotation (applied after LookAt, mirrors
    // Previewd's localRotMatrix). Only non-zero for MDX presets
    // or scripted shots. ----
    void  SetLocalEuler(float yaw, float pitch, float roll) {
        localYaw_ = yaw; localPitch_ = pitch; localRoll_ = roll;
    }

    // ---- Mode switching ----
    Mode GetMode() const { return mode_; }
    void SetOrbitalMode() { mode_ = Mode::Orbital; }
    void SetDirectPose(const Vector3f& pos, const Vector3f& target,
                       float rollRad = 0.0f) {
        mode_ = Mode::Direct;
        directPosition_ = pos;
        directTarget_   = target;
        roll_           = rollRad;
    }

    // ============================================================
    // Matrix builders.
    //
    // The HD / SD_on_HD stack MUST consume the LH pair; the shipped
    // BLS was compiled against SgCompat + diagonal FOV. The RH pair
    // stays for SD Path A and UI overlays that were authored against
    // D3D-standard RH math.
    // ============================================================

    // LH view. Orbital mode derives up from yaw/pitch/roll; Direct
    // mode (MDX presets) derives up from the look direction since
    // the orbital angles are stale.
    Matrix44f ViewLH() const {
        Vector3f up = (mode_ == Mode::Direct)
                          ? ComputeUpFromLookDirection()
                          : ComputeUpFromAngles();
        Matrix44f view = Matrix44f::look_at_lh_sgcompat(GetSource(), GetTarget(), up);
        if (localYaw_ != 0.0f || localPitch_ != 0.0f || localRoll_ != 0.0f) {
            view = view * LocalEulerMatrix();
        }
        return view;
    }
    // RH view kept for legacy SD Path A and UI overlays.
    Matrix44f ViewRH() const {
        return Matrix44f::look_at_rh(GetSource(), GetTarget(), GetUp());
    }

    // LH projection matching Previewd's GxuXformCreateProjection:
    // diagonal FOV, symmetric NDC, LH forward-is-+Z.
    Matrix44f ProjectionLH(float aspect) const {
        return Matrix44f::perspective_diag_sgcompat(fovDiagonal_, aspect, zNear_, zFar_);
    }
    // RH projection with an equivalent vertical FOV so swapping
    // stacks doesn't zoom.
    Matrix44f ProjectionRH(float aspect) const {
        const float invDiag = 1.0f / std::sqrt(aspect * aspect + 1.0f);
        const float fovY    = 2.0f * std::atan(std::tan(0.5f * fovDiagonal_ * invDiag));
        return Matrix44f::perspective_fov_rh(fovY, aspect, zNear_, zFar_);
    }

    // Single-call joint build. Callers that need both handedness
    // sides in the same frame (e.g. a frame that does both HD draws
    // and UI overlays) call this once to amortise the trig.
    CameraMatrices Compute(float aspect) const {
        CameraMatrices m;
        m.viewLH = ViewLH();
        m.viewRH = ViewRH();
        m.projLH = ProjectionLH(aspect);
        m.projRH = ProjectionRH(aspect);
        return m;
    }

    // Legacy name kept so callers that only need the RH view don't
    // have to be rewritten in the same CL as the handedness switch.
    Matrix44f GetViewMatrix() const { return ViewRH(); }
    Vector3f GetUp() const { return Vector3f(0.f, 0.f, 1.f); }

private:
    // Direct-mode up: orthonormal basis from look direction + world-up,
    // rolled around forward by `roll_`.
    Vector3f ComputeUpFromLookDirection() const {
        Vector3f forward = GetTarget() - GetSource();
        const float fLen = forward.length();
        if (fLen < 1e-4f) return {0.0f, 0.0f, 1.0f};
        forward = forward / fLen;

        Vector3f worldUp{0.0f, 0.0f, 1.0f};
        Vector3f right = cross(forward, worldUp);
        // Look nearly parallel to world-up — pick a different reference.
        if (right.length_squared() < 1e-6f) {
            right = cross(forward, Vector3f{1.0f, 0.0f, 0.0f});
        }
        right = right.normalized();
        Vector3f upBase = cross(right, forward).normalized();

        if (roll_ == 0.0f) return upBase;
        const float c = std::cos(roll_), s = std::sin(roll_);
        return {
            c * upBase.x + s * right.x,
            c * upBase.y + s * right.y,
            c * upBase.z + s * right.z,
        };
    }

    // Matches Previewd's CCamera::SetupWorldProjection up-vector math.
    Vector3f ComputeUpFromAngles() const {
        const float sinDir = std::sin(yaw_),   cosDir = std::cos(yaw_);
        const float sinAoa = std::sin(pitch_), cosAoa = std::cos(pitch_);
        const float sinRoll = std::sin(roll_), cosRoll = std::cos(roll_);
        Vector3f noRoll(-cosDir * sinAoa, -sinDir * sinAoa,  cosAoa);
        Vector3f allRoll(sinDir,          -cosDir,           0.0f);
        return {
            cosRoll * noRoll.x + sinRoll * allRoll.x,
            cosRoll * noRoll.y + sinRoll * allRoll.y,
            cosRoll * noRoll.z + sinRoll * allRoll.z
        };
    }

    // ZYX-order Euler rotation as a 4×4 matrix, mirroring Previewd's
    // C33Matrix::FromEulerAnglesZYX expanded to 4×4 with identity
    // translation. Row-major storage; multiplies on the right of the
    // view matrix (view * LocalEuler).
    Matrix44f LocalEulerMatrix() const {
        const float cz = std::cos(localYaw_),   sz = std::sin(localYaw_);
        const float cy = std::cos(localPitch_), sy = std::sin(localPitch_);
        const float cx = std::cos(localRoll_),  sx = std::sin(localRoll_);
        Matrix44f r{};
        r.data[0][0] =  cz*cy;
        r.data[0][1] =  cz*sy*sx - sz*cx;
        r.data[0][2] =  cz*sy*cx + sz*sx;
        r.data[0][3] = 0.0f;
        r.data[1][0] =  sz*cy;
        r.data[1][1] =  sz*sy*sx + cz*cx;
        r.data[1][2] =  sz*sy*cx - cz*sx;
        r.data[1][3] = 0.0f;
        r.data[2][0] = -sy;
        r.data[2][1] =  cy*sx;
        r.data[2][2] =  cy*cx;
        r.data[2][3] = 0.0f;
        r.data[3][0] = 0.0f; r.data[3][1] = 0.0f; r.data[3][2] = 0.0f; r.data[3][3] = 1.0f;
        return r;
    }

    // Orbital state.
    float pitch_;
    float yaw_;
    float distance_;
    Vector3f target_;

    // Direct state (used when mode_ == Direct).
    Mode mode_ = Mode::Orbital;
    Vector3f directPosition_;
    Vector3f directTarget_;

    // Shared state.
    float roll_ = 0.0f;
    float localYaw_ = 0.0f, localPitch_ = 0.0f, localRoll_ = 0.0f;
    float fovDiagonal_ = kDefaultFovDiagonal;
    float zNear_ = kDefaultNearZ;
    float zFar_  = kDefaultFarZ;
};

} // namespace WhiteoutDex
