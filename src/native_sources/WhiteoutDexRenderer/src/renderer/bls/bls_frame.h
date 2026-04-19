#pragma once
// ============================================================================
// BlsFrame — builds CB payloads for Previewd's SD shader pipeline.
// Path A (SD-in-SD-mode) is what we use; Path B is reserved for HD mode.
// ============================================================================

#include "bls_cb_layout.h"
#include "bls_mat_params.h"
#include "types.h"

#include <cstdint>

namespace WhiteoutDex::bls {

struct FrameInputs {
    Matrix44f  world          = Matrix44f::identity();
    Matrix44f  view           = Matrix44f::identity();
    Matrix44f  projection     = Matrix44f::identity();

    Vector4f   fogParams      = {0, 0, 0, 0};     // {start, end, density, 0}
    Vector4f   fogColor       = {0, 0, 0, 0};     // sRGB-linear approx
    Vector4f   viewportRect   = {1, 1, 0, 0};     // {w, h, x, 1-yh}

    float      effectTime     = 0.0f;
    int32_t    numLights      = 0;
    // Enable Kaplanyan/Hill specular-AA on the IBL roughness selector.
    // The shipped HD PS at re_shaders/hd/hd_mesh_ps.hlsl line 509 picks
    // `r4.y = cb2[20].w ? specAA_roughness : raw_roughness;` — i.e.
    // useNdf != 0 → add screen-space normal-derivative variance to
    // roughness before the mip-select. Without this, low-roughness
    // surfaces sample mip 0 of the specular cube and the environment's
    // horizon projects as a hard screen-aligned reflection seam. With
    // it, the variance term floors the effective roughness high enough
    // to pick a pre-filtered mip and the seam dissolves into the blur.
    int32_t    useNdf         = 1;

    // HD IBL: highest valid mip index of the "from" / "to" env cube arrays,
    // plus the blend scalar between them. The HD PS clamps its roughness->
    // mip lookup to [0, envMipEnd]. Leave zeros when no env probe is bound
    // (forces mip 0 reads, which are fine with our default grey probe).
    float      envFromMipEnd  = 0.0f;
    float      envToMipEnd    = 0.0f;
    float      envTransitionT = 0.0f;

    ShaderTexMtx texMtx0      = {{ Vector4f{1, 0, 0, 0}, Vector4f{0, 1, 0, 0} }};
    ShaderTexMtx texMtx1      = {{ Vector4f{1, 0, 0, 0}, Vector4f{0, 1, 0, 0} }};

    ShaderLight lights[kMaxLights] = {};
};

// Path A (SD shader, SD mode). VS = 208 + 48*nLights. PS = 48 B.
void BuildSdVsCbA(SdVsCbA& out, const FrameInputs& in, const MatParams& mat);
void BuildSdPsCbA(SdPsCbA& out, const FrameInputs& in, const MatParams& mat);

// Path B (HD / SD_on_HD). VS = 288 B (shared). PS = 336 + 64*nLights.
// HD and SD_on_HD share the VS CB layout but split the PS CB between a
// lightweight HD layout (PBR fields directly addressable) and the
// padded SD_on_HD layout that mirrors the engine's legacy CB slots.
void BuildHdVsCb    (HdVsCb&     out, const FrameInputs& in, const MatParams& mat);
void BuildHdPsCb    (HdPsCb&     out, const FrameInputs& in, const MatParams& mat);
void BuildSdOnHdPsCb(SdOnHdPsCb& out, const FrameInputs& in, const MatParams& mat);

// Bone palette helpers (used by skinned geosets on both paths).
void PackBone(ShaderBone& out, const Matrix44f& m);
void BuildBonePalette(BonePaletteCb& out, const Matrix44f* src, int numBones);
void PackBoneVertex(BoneVertex& out, const int indices[4], const float weights[4]);

// Identity 2x3 UV transform (rows 0,1 of a 2x4 affine matrix where .z is
// ignored by the shader and .w is the translation component).
inline ShaderTexMtx IdentityTexMtx() {
    return { { Vector4f{1, 0, 0, 0}, Vector4f{0, 1, 0, 0} } };
}

// Build the SD VS tex-anim matrix matching Previewd's AnimateTextureMap
// stack (RotateTexture -> ScaleTexture -> TranslateTexture, with rotate
// and scale pivoting on (0.5, 0.5); translation applied in UV units).
// Rotation is reduced to a Z-axis angle via 2*atan2(q.z, q.w) -- same
// convention the adapter already uses for the Slang path. WC3 MDX only
// authors Z-aligned UV rotation so this is safe.
ShaderTexMtx ComposeTexAnimMatrix(const Quaternion& rot,
                                  const Vector3f&   scale,
                                  const Vector3f&   trans);

} // namespace WhiteoutDex::bls
