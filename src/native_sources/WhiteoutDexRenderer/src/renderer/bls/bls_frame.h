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
    int32_t    useNdf         = 0;

    ShaderTexMtx texMtx0      = {{ Vector4f{1, 0, 0, 0}, Vector4f{0, 1, 0, 0} }};
    ShaderTexMtx texMtx1      = {{ Vector4f{1, 0, 0, 0}, Vector4f{0, 1, 0, 0} }};

    ShaderLight lights[kMaxLights] = {};
};

// Path A (SD shader, SD mode). VS = 208 + 48*nLights. PS = 48 B.
void BuildSdVsCbA(SdVsCbA& out, const FrameInputs& in, const MatParams& mat);
void BuildSdPsCbA(SdPsCbA& out, const FrameInputs& in, const MatParams& mat);

// Path B (SD_on_HD / HD / ...). VS = 288 B. PS = 336 + 48*nLights.
void BuildSdVsCbB(SdVsCbB& out, const FrameInputs& in, const MatParams& mat);
void BuildSdPsCbB(SdPsCbB& out, const FrameInputs& in, const MatParams& mat);

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
