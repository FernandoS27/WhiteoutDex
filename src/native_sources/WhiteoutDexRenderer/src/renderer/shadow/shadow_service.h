#pragma once
// ============================================================================
// ShadowService — owns CSM depth targets + computed cascade VPs.
//
// Lifecycle mirrors PopcornService / DncService:
//   * Constructed once with a non-owning pointer to the gfx device.
//   * EnsureTargets() lazily creates / resizes the per-cascade depth
//     maps when params change.
//   * Update() runs once per frame from the host (after camera +
//     light direction have been resolved) to recompute cascade VPs.
//   * The shadow render pass binds depthTarget(c) + sets cascadeVP(c)
//     in vsCB2; the main HD pass binds the same handles as SRVs.
//
// Engine reference: WorldShadowUpdate @ preview.exe 0x7ff609b0fda0.
// The math (light-view, PSSM splits, frustum-corner AABB, texel snap,
// ortho proj) follows that decompile verbatim. See docs/SHADOW_MAP_PLAN.md
// for the full derivation.
// ============================================================================

#include "shadow_params.h"
#include "renderer/types.h"
#include "gfx/gfx.h"

#include <array>

namespace WhiteoutDex::shadow {

// One cascade's persistent state. Owned by ShadowService.
struct Cascade {
    gfx::TextureHandle depth         = gfx::TextureHandle::Invalid;
    Matrix44f          worldToClip   = Matrix44f::identity();   // cascadeProj * lightView
    int                resolution    = 0;
};

class ShadowService {
public:
    explicit ShadowService(gfx::IGFXDevice* gfx);
    ~ShadowService();

    ShadowService(const ShadowService&) = delete;
    ShadowService& operator=(const ShadowService&) = delete;

    // ---- Configuration ----

    void                SetParams(const ShadowParams& p);
    const ShadowParams& Params() const { return params_; }

    // Convenience: just toggle the master enable, leaving everything
    // else alone. Cheap; safe to call from the UI thread.
    void  SetEnabled(bool on);
    bool  IsEnabled() const { return params_.enabled; }

    // ---- Per-frame ----

    // Recompute the cascade VPs for the current camera + light. Pass
    // the active camera's view + projection (LH form, the same
    // matrices the HD pass uses), the directional light's world-space
    // direction, and the scene bounds (centre + radius) the cascades
    // need to cover.
    //
    // For the model viewer:
    //   sceneCenterWS = focus actor's world-transform translation
    //   sceneRadius   = focus actor's bounds-sphere radius
    //
    // When terrain lands, the host swaps these for the union AABB of
    // terrain + every visible actor — the math is unchanged.
    void Update(const Matrix44f& cameraViewLH,
                const Matrix44f& cameraProjLH,
                float            cameraNearZ,
                float            cameraFarZ,
                const Vector3f&  lightDirWS,
                const Vector3f&  sceneCenterWS,
                float            sceneRadius);

    // ---- Per-cascade access ----

    int                cascadeCount() const { return params_.cascadeCount; }
    gfx::TextureHandle depthTarget (int c) const;
    const Matrix44f&   cascadeVP   (int c) const;

    // Pack the active cascade VPs into the BLS vsCB1 layout. Unused
    // cascades are filled with identity so the shader's
    // `isInShadowFrustum` check rejects them (clip.z >= 1 → outside,
    // returns 1.0 / fully lit).
    template <typename HdShadowCascadesCb>
    void FillVsCb(HdShadowCascadesCb& out) const {
        out.cascade0 = cascadeVP(0);
        out.cascade1 = cascadeVP(1);
        out.cascade2 = cascadeVP(2);
    }

private:
    void EnsureTargets();
    void DestroyTargets();

    static Matrix44f BuildLightViewLH(const Vector3f& center, const Vector3f& lightDir,
                                      float casterHeight);
    // Off-center LH ortho projection for an axis-aligned [l,r]×[b,t]×[n,f]
    // frustum in light space.
    static Matrix44f OrthoLHOffcenter(float l, float r, float b, float t, float n, float f);

    // Compute a partial perspective projection covering the slice
    // [nearSplit, farSplit] of the camera's z range, using the same
    // LH SgCompat builder the camera uses for its full-frame proj.
    Matrix44f CameraSliceProjLH(const Matrix44f& camProj,
                                float            cameraNearZ,
                                float            cameraFarZ,
                                float            nearSplit,
                                float            farSplit) const;

    // Extract the 8 world-space corners of the camera frustum slice.
    // Uses inverse(view*proj) to back-project the 8 NDC corners.
    static void FrustumCornersWS(const Matrix44f& view, const Matrix44f& proj,
                                 Vector3f outCorners[8]);

    gfx::IGFXDevice*               gfx_ = nullptr;
    ShadowParams                   params_;
    std::array<Cascade, 3>         cascades_{};
    Matrix44f                      lightView_ = Matrix44f::identity();
    bool                           targetsValid_ = false;
};

} // namespace WhiteoutDex::shadow
