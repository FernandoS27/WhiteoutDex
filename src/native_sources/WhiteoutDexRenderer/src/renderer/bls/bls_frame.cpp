#include "bls_frame.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace WhiteoutDex::bls {

namespace {

// engine's IMatAlphaRef table: Blend->0, AlphaKey->192, others->0.
inline float AlphaRefFor(GxMatAlpha a) {
    return (a == GxMatAlpha::AlphaKey) ? (192.0f / 255.0f) : 0.0f;
}

} // namespace

// ---------- Path A (SD in SD mode) -----------------------------------------
//
// The BLS SD VS multiplies via `mul(matrix, col_vector)` (see transformClipPos
// in vs_body.slang). With HLSL's default column_major layout, uploading our
// row-major Matrix44f directly puts its rows into cb0[k] slots, which HLSL
// then reads as "columns" of the shader-side matrix. The math works out so
// that `mul(cb.M, float4(pos,1))` equals `pos * M_cpp` in our row convention.
// Hence: NO transpose on upload.

void BuildSdVsCbA(SdVsCbA& out, const FrameInputs& in, const MatParams& mat) {
    // IStateSync Path A (Previewd 0x1403f7580) uploads
    // (prismWorld * prismView) at cb0[0..3] -- the decompiled Slang struct
    // labels the field `world` but the engine packs WORLD-VIEW there, which
    // means the VS output o3 ("worldPos") is really in VIEW space. Light
    // positions in cb0[13+] are also view-space (CGxLightToShaderLight
    // multiplies by prismViewMat). We mirror that here so lighting math
    // resolves in a single consistent frame.
    const Matrix44f wv  = in.world * in.view;
    const Matrix44f wvp = wv * in.projection;

    out.world         = wv;    // cb0[0..3] = worldView, despite the name
    out.worldViewProj = wvp;   // cb0[4..7]
    out.diffuseColor  = mat.diffuseColor;
    out.texMtx0       = in.texMtx0;
    out.texMtx1       = in.texMtx1;

    const int n = std::clamp(in.numLights, 0, kMaxLights);
    for (int i = 0; i < n; ++i) out.lights[i] = in.lights[i];
    for (int i = n; i < kMaxLights; ++i) out.lights[i] = {};
}

void BuildSdPsCbA(SdPsCbA& out, const FrameInputs& in, const MatParams& mat) {
    std::memset(&out, 0, sizeof(out));
    out.alphaRef  = AlphaRefFor(mat.alpha);
    out.fogParams = in.fogParams;
    out.fogColor  = in.fogColor;
}

// ---------- Path B (SD_on_HD / HD, reserved) -------------------------------

void BuildSdVsCbB(SdVsCbB& out, const FrameInputs& in, const MatParams& mat) {
    const Matrix44f wv  = in.world * in.view;
    const Matrix44f wvp = wv * in.projection;

    out.world         = in.world;
    out.worldView     = wv;
    out.worldViewProj = wvp;
    out.misc          = { in.effectTime, mat.popcornScale, mat.vertexPad.x, mat.vertexPad.y };
    out.diffuseColor  = mat.diffuseColor;
    out.texMtx0       = in.texMtx0;
    out.texMtx1       = in.texMtx1;
}

void BuildSdPsCbB(SdPsCbB& out, const FrameInputs& in, const MatParams& mat) {
    std::memset(&out, 0, sizeof(out));
    out.alphaRef     = AlphaRefFor(mat.alpha);
    out.fogParams    = in.fogParams;
    out.fogColor     = in.fogColor;
    out.worldView    = in.world * in.view;
    out.viewInverse  = Matrix44f::inverse(in.view);
    out.projection   = in.projection;
    out.viewportRect = in.viewportRect;
    out.effectTime   = in.effectTime;
    out.emissiveGain = mat.emissiveGain;
    out.numLights    = in.numLights;
    out.useNdf       = in.useNdf;

    const int n = std::clamp(in.numLights, 0, kMaxLights);
    for (int i = 0; i < n; ++i) out.lights[i] = in.lights[i];
    for (int i = n; i < kMaxLights; ++i) out.lights[i] = {};
}

// ---------- Bone palette ---------------------------------------------------

void PackBone(ShaderBone& out, const Matrix44f& m) {
    const Matrix44f t = m.transpose();
    out.row0 = { t.data[0][0], t.data[0][1], t.data[0][2], t.data[0][3] };
    out.row1 = { t.data[1][0], t.data[1][1], t.data[1][2], t.data[1][3] };
    out.row2 = { t.data[2][0], t.data[2][1], t.data[2][2], t.data[2][3] };
}

void BuildBonePalette(BonePaletteCb& out, const Matrix44f* src, int numBones) {
    PackBone(out.bones[0], Matrix44f::identity());
    const int n = std::clamp(numBones, 0, kMaxBones);
    for (int i = 0; i < n; ++i) PackBone(out.bones[i], src[i]);
    for (int i = std::max(1, n); i < kMaxBones; ++i) {
        PackBone(out.bones[i], Matrix44f::identity());
    }
}

ShaderTexMtx ComposeTexAnimMatrix(const Quaternion& q,
                                  const Vector3f&   s,
                                  const Vector3f&   t) {
    // 2D affine composition: T * S_around_center * R_around_center.
    // Closed form avoids building intermediate 4x4s. The shader reads
    // each row as (a, b, _, c) and applies dot(row.xyw, (u, v, 1)).
    const float ang = 2.0f * std::atan2(q.z, q.w);
    const float c   = std::cos(ang);
    const float si  = std::sin(ang);

    // Combined rotate * scale (order: rotate first, then scale):
    //   R = [ c, -s; s, c ],   S = [ sx, 0; 0, sy ]
    //   (S * R) rows: [ sx*c, -sx*s; sy*s, sy*c ]
    const float a =  s.x * c;
    const float b = -s.x * si;
    const float d =  s.y * si;
    const float e =  s.y * c;

    // Pivot correction so (0.5, 0.5) maps to itself under rotate+scale.
    // For a 2x2 linear part M, the translation that keeps point p fixed
    // is (p - M*p). Here p = (0.5, 0.5).
    const float cc = 0.5f - (a * 0.5f + b * 0.5f) + t.x;
    const float ff = 0.5f - (d * 0.5f + e * 0.5f) + t.y;

    ShaderTexMtx m{};
    m.rows[0] = { a, b, 0.0f, cc };
    m.rows[1] = { d, e, 0.0f, ff };
    return m;
}

void PackBoneVertex(BoneVertex& out, const int indices[4], const float weights[4]) {
    float ws[4]; float total = 0;
    for (int i = 0; i < 4; ++i) {
        float w = (weights[i] > 0 && std::isfinite(weights[i])) ? weights[i] : 0.0f;
        ws[i] = w; total += w;
    }
    if (total > 1e-6f) {
        float scale = 255.0f / total;
        int   acc = 0, last = 0;
        for (int i = 0; i < 4; ++i) {
            int q = std::clamp(static_cast<int>(std::lround(ws[i] * scale)), 0, 255);
            out.weights[i] = static_cast<uint8_t>(q);
            acc += q; if (q > 0) last = i;
        }
        if (acc != 255) {
            int adj = std::clamp(static_cast<int>(out.weights[last]) + (255 - acc), 0, 255);
            out.weights[last] = static_cast<uint8_t>(adj);
        }
    } else {
        out.weights[0] = 255;
        out.weights[1] = 0; out.weights[2] = 0; out.weights[3] = 0;
    }
    for (int i = 0; i < 4; ++i) {
        int idx = std::clamp(indices[i], 0, 255);
        out.indices[i] = static_cast<uint8_t>(idx);
    }
}

} // namespace WhiteoutDex::bls
