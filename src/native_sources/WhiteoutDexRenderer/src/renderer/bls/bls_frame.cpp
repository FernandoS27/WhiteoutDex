#include "bls_frame.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace WhiteoutDex::bls {

namespace {

// Per-blend-mode alpha discard threshold. AlphaKey cuts at 192/255 so
// TEAM_GLOW / cutout sprites keep their hard edges; Blend / Add /
// Modulate(2X) use a 4/255 near-zero cleanup; Opaque is unused.
inline float AlphaRefFor(GxMatAlpha a) {
    switch (a) {
        case GxMatAlpha::AlphaKey:   return 192.0f / 255.0f;
        case GxMatAlpha::Blend:
        case GxMatAlpha::Add:
        case GxMatAlpha::Modulate:
        case GxMatAlpha::Modulate2X: return 4.0f / 255.0f;
        case GxMatAlpha::Opaque:
        default:                     return 0.0f;
    }
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

// ---------- Path B (HD / SD_on_HD) -----------------------------------------
//
// Both HD and SD_on_HD VS consume the same CB layout (HdVsCb). PS CBs
// diverge only in which fields are meaningful vs padding; the base size is
// identical (336 + 64*N bytes).

void BuildHdVsCb(HdVsCb& out, const FrameInputs& in, const MatParams& mat) {
    // Upload row-major matrices directly -- the Slang HD VS uses the same
    // mul(matrix, col_vec) convention as the SD VS (see BLS_ShaderABI.md):
    // HLSL's default column_major interpretation of our row-major blobs
    // yields the correct result without a CPU-side transpose.
    const Matrix44f wv  = in.world * in.view;
    const Matrix44f wvp = wv * in.projection;

    // Field names match Previewd semantics (see HdVsCb in bls_cb_layout.h):
    //   cb2[0..3]  = world         = m_prismWorldMat
    //   cb2[4..7]  = worldView     = m_prismWorldMat * m_prismViewMat
    //   cb2[8..11] = worldViewProj = above * m_prismProjectionMat
    out.world         = in.world;
    out.worldView     = wv;
    out.worldViewProj = wvp;
    // misc.xyzw = {effectTime, popcornScale, clipHeight, underWater} per
    // the Slang field order. clipHeight+underWater feed the fog-clip plane
    // via computeFogDepth = (viewZ - clipHeight) * underWater; the HD PS
    // discards fragments where that goes negative. With underWater=0 the
    // product is zero regardless of clipHeight, so backfaceDiscard never
    // fires — leave clipHeight=0 too to keep the slot visibly neutral.
    out.misc          = { in.effectTime, mat.popcornScale, 0.0f, 0.0f };
    out.diffuseColor  = mat.diffuseColor;
    out.texMtx0       = in.texMtx0;
    out.texMtx1       = in.texMtx1;
}

void BuildHdPsCb(HdPsCb& out, const FrameInputs& in, const MatParams& mat) {
    std::memset(&out, 0, sizeof(out));
    out.alphaRef     = AlphaRefFor(mat.alpha);
    out.fogParams    = in.fogParams;
    out.fogColor     = in.fogColor;
    out.worldView    = in.world * in.view;
    out.view         = in.view;
    out.projection   = in.projection;
    out.viewportRect = in.viewportRect;
    out.effectTime   = in.effectTime;
    out.emissiveGain = mat.emissiveGain;

    // lightCount is declared `float` in cb_structs.slang::PSPerDraw but the
    // IBL body reads it via `asuint(psCB2.lightCount)` -- see ps_ibl.slang
    // around the `while (true) { if (lightIdx >= asuint(psCB2.lightCount))
    // break; }` loop and the `asuint(psCB2.lightCount) > 0` first-light
    // test. Writing the value as an IEEE-754 float would give bit pattern
    // 0x3F800000 for 1.0, interpreted by the shader as ~1.07B iterations
    // -> instant GPU TDR. Pack as a uint bit-reinterpret, same pattern as
    // SdOnHdPsCb.lightCountSlot.z. Verified against CGxDevice::IStateSync
    // in Previewd (0x1403f88da): `v46 = v51;` -- v46 is int in the
    // lightCount slot, v51 is the incremented light counter.
    const int nLights = std::clamp(in.numLights, 0, kMaxLights);
    const uint32_t countBits = static_cast<uint32_t>(nLights);
    std::memcpy(&out.lightCount, &countBits, sizeof(float));

    out.useNdf       = in.useNdf ? 1.0f : 0.0f;
    // Per-material HD pixel parameters, mirroring Previewd's IStateSync
    // upload of p_m_material->m_pixelParams (C4Vector + pad2 + vec4Param3
    // at CGxMatParams+0x38). Layout verified against the IDA dump of
    // CGxMatParams::PixelParams:
    //   pixelParams1 = {inverseSoftnessDistance, cloak, fresnelTeamColor, pad=0}
    //   pixelParams2 = pad2                                 (always zero)
    //   fresnelColor = {fresnelR, fresnelG, fresnelB, fresnelA}
    // inverseSoftness is the PS alpha scale (result.w *= inverseSoftness
    // at the end of the body) — 1.0 for visible draws. cloak / fresnel*
    // come from the MDX layer (mat.fresnelColor etc.); default zeros
    // leave the fresnel-overlay and cloak paths dormant.
    out.pixelParams1 = { mat.inverseSoftness,
                         mat.cloakAmount,
                         mat.fresnelTeamColor,
                         0.0f };
    // pixelParams2 is the engine's `pad2`; memset'd to zero above.
    out.fresnelColor = { mat.fresnelColor.x,
                         mat.fresnelColor.y,
                         mat.fresnelColor.z,
                         mat.fresnelOpacity };
    // envMapParams.xyzw = { envFromMipEnd, envToMipEnd, envTransitionT, 0 }.
    // The IBL shader's fast-out at sampleIBL() triggers when
    // `envFromMipEnd * envToMipEnd == 0`, so either zero disables all IBL
    // contribution. Our default probe has 5 mips, so DefaultEnvProbeEndMip()
    // = 4 enables the sample path and caps the roughness->mip remap.
    out.envMapParams = { in.envFromMipEnd, in.envToMipEnd, in.envTransitionT, 0.0f };

    // ShaderLight layout per cb_structs.slang::ShaderLight:
    //   ambient.xyz,  _pad0        (= 0)
    //   diffuse.xyz,  _pad1        (= 0)
    //   position.xyz, type         (0 = directional, >0 = point)
    //   _pad2                      (_pad2.x = per-light ambient weight read
    //                               by ps_ibl.slang blizzardAmbientBlend)
    // CGxLightToShaderLight (0x1403fbc00 in Previewd) only writes 48 B
    // (ambient/diffuse/position), so the engine's live shaders -- which
    // are a year behind the Slang sources in wc3_shaders -- never had
    // this ambient-weight term in the IBL body at all. Running the
    // current Slang shaders with _pad2 = 0 zeroes the IBL diffuse
    // contribution and produces a nearly-black render. Set _pad2.x = 1.0
    // so the IBL ambient mix contributes as the Slang body expects;
    // revisit once the shipped BLS is regenerated from the new sources.
    for (int i = 0; i < nLights; ++i) {
        out.lights[i] = in.lights[i];
        out.lights[i]._pad = { 0.25f, 0.0f, 0.0f, 0.0f };
    }
    for (int i = nLights; i < kMaxLights; ++i) out.lights[i] = {};
}

void BuildSdOnHdPsCb(SdOnHdPsCb& out, const FrameInputs& in, const MatParams& mat) {
    std::memset(&out, 0, sizeof(out));
    out.alphaRef  = AlphaRefFor(mat.alpha);
    out.fogParams = in.fogParams;
    out.fogColor  = in.fogColor;

    // invViewRow0..2: rows of the transposed view->world rotation,
    // consumed by sd_on_hd_ps.slang to orient IBL cubemap samples. We
    // take the inverse of the view matrix and copy the upper-3x3
    // rotation rows; translation is irrelevant for direction reflection.
    const Matrix44f invView = Matrix44f::inverse(in.view);
    out.invViewRow0 = { invView.data[0][0], invView.data[0][1], invView.data[0][2], invView.data[0][3] };
    out.invViewRow1 = { invView.data[1][0], invView.data[1][1], invView.data[1][2], invView.data[1][3] };
    out.invViewRow2 = { invView.data[2][0], invView.data[2][1], invView.data[2][2], invView.data[2][3] };

    // See BuildHdPsCb re: inverseSoftness=1 rescuing the final alpha.
    out.pixelParams1 = {1.0f, 0.0f, 0.0f, 0.0f};
    out.envMapParams = { in.envFromMipEnd, in.envToMipEnd, in.envTransitionT, 0.0f };

    // lightCountSlot.z holds the uint-reinterpret of numLights.
    const int n = std::clamp(in.numLights, 0, kMaxLights);
    const uint32_t countBits = static_cast<uint32_t>(n);
    float countAsFloat;
    std::memcpy(&countAsFloat, &countBits, sizeof(float));
    out.lightCountSlot = {0, 0, countAsFloat, 0};

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
