// ============================================================================
// ShadowService — implementation
// ============================================================================

#include "shadow_service.h"

#include <algorithm>
#include <cmath>

namespace WhiteoutDex::shadow {

namespace {

// Multiply v (column vector w=1) by a Matrix44f stored in the
// renderer's row-major / row-vector convention (`v_new = v * M`).
// Used to push frustum corners through the inverse(view*proj).
Vector3f MulPoint(const Matrix44f& m, const Vector3f& v) {
    const float x = v.x * m.data[0][0] + v.y * m.data[1][0]
                  + v.z * m.data[2][0] +        m.data[3][0];
    const float y = v.x * m.data[0][1] + v.y * m.data[1][1]
                  + v.z * m.data[2][1] +        m.data[3][1];
    const float z = v.x * m.data[0][2] + v.y * m.data[1][2]
                  + v.z * m.data[2][2] +        m.data[3][2];
    const float w = v.x * m.data[0][3] + v.y * m.data[1][3]
                  + v.z * m.data[2][3] +        m.data[3][3];
    const float invW = (std::abs(w) > 1.0e-6f) ? (1.0f / w) : 1.0f;
    return { x * invW, y * invW, z * invW };
}

} // namespace

ShadowService::ShadowService(gfx::IGFXDevice* gfx) : gfx_(gfx) {}

ShadowService::~ShadowService() {
    DestroyTargets();
}

void ShadowService::SetParams(const ShadowParams& p) {
    const bool needsRealloc =
        p.cascadeResolution != params_.cascadeResolution ||
        p.cascadeCount      != params_.cascadeCount;
    params_ = p;
    if (needsRealloc) {
        DestroyTargets();
        targetsValid_ = false;
    }
}

void ShadowService::SetEnabled(bool on) {
    params_.enabled = on;
}

gfx::TextureHandle ShadowService::depthTarget(int c) const {
    if (c < 0 || c >= 3) return gfx::TextureHandle::Invalid;
    return cascades_[c].depth;
}

const Matrix44f& ShadowService::cascadeVP(int c) const {
    if (c < 0 || c >= 3) {
        static const Matrix44f kIdentity = Matrix44f::identity();
        return kIdentity;
    }
    return cascades_[c].worldToClip;
}

void ShadowService::EnsureTargets() {
    if (targetsValid_) return;
    if (!gfx_) return;
    const int res = std::max(64, params_.cascadeResolution);
    for (int c = 0; c < 3; ++c) {
        if (cascades_[c].depth != gfx::TextureHandle::Invalid &&
            cascades_[c].resolution == res) {
            continue;
        }
        if (cascades_[c].depth != gfx::TextureHandle::Invalid) {
            gfx_->Destroy(cascades_[c].depth);
        }
        // R32-typed depth, dual-aliased: both DSV (for the shadow
        // pass) and SRV (for the HD PS to SampleCmpLevelZero against).
        // CreateDepthTarget hardcodes DepthStencil-only, which makes
        // the d3d12 backend set DENY_SHADER_RESOURCE on the resource
        // and skip the SRV view altogether — sampling that slot in
        // the PS hangs the GPU on some drivers. Going through
        // CreateTexture with both flags gives us both views on one
        // resource (typeless internally, depth-as-DSV + r32-as-SRV).
        gfx::TextureDesc td{};
        td.width     = res;
        td.height    = res;
        td.mipLevels = 1;
        td.format    = gfx::Format::D32_FLOAT;
        td.usage     = gfx::TextureUsage::DepthStencil
                     | gfx::TextureUsage::ShaderResource;
        cascades_[c].depth = gfx_->CreateTexture(td, nullptr);
        cascades_[c].resolution = res;
        cascades_[c].worldToClip = Matrix44f::identity();
    }
    targetsValid_ = true;
}

void ShadowService::DestroyTargets() {
    if (!gfx_) return;
    for (auto& c : cascades_) {
        if (c.depth != gfx::TextureHandle::Invalid) {
            gfx_->Destroy(c.depth);
            c.depth = gfx::TextureHandle::Invalid;
        }
        c.resolution = 0;
    }
}

// ----------------------------------------------------------------------------
// Math helpers
// ----------------------------------------------------------------------------

Matrix44f ShadowService::BuildLightViewLH(const Vector3f& center,
                                          const Vector3f& lightDirIn,
                                          float casterHeight) {
    // Normalise the light direction. Treat zero / near-zero as
    // straight-down (+Z is up in WC3 default; light points toward
    // -Z) so a degenerate input still produces a usable view.
    Vector3f L = lightDirIn;
    float n2 = L.x * L.x + L.y * L.y + L.z * L.z;
    if (n2 < 1.0e-12f) { L = { 0.0f, 0.0f, -1.0f }; }
    else { const float invLen = 1.0f / std::sqrt(n2); L = { L.x*invLen, L.y*invLen, L.z*invLen }; }

    // Light "eye" sits along -L from the scene centre by casterHeight.
    const Vector3f eye = {
        center.x - L.x * casterHeight,
        center.y - L.y * casterHeight,
        center.z - L.z * casterHeight,
    };

    // Up vector: pick world-Z unless the light is too parallel to it,
    // then fall back to world-Y. Mirrors the engine's stable-up trick
    // (preview.exe 0x7ff609b0fea1).
    const Vector3f worldUp = (std::abs(L.z) < 0.95f)
                                 ? Vector3f{0.0f, 0.0f, 1.0f}
                                 : Vector3f{0.0f, 1.0f, 0.0f};
    return Matrix44f::look_at_lh_sgcompat(eye, center, worldUp);
}

Matrix44f ShadowService::OrthoLHOffcenter(float l, float r, float b, float t,
                                          float n, float f) {
    // Row-major / row-vector convention to match the rest of the
    // renderer. Maps view-space [l,r]×[b,t]×[n,f] → clip-space
    // [-1,1]×[-1,1]×[0,1] (LH, D3D-style).
    Matrix44f m{};
    const float dx = r - l;
    const float dy = t - b;
    const float dz = f - n;
    m.data[0][0] =  2.0f / dx;
    m.data[1][1] =  2.0f / dy;
    m.data[2][2] =  1.0f / dz;
    m.data[3][0] = -(r + l) / dx;
    m.data[3][1] = -(t + b) / dy;
    m.data[3][2] = -n       / dz;
    m.data[3][3] =  1.0f;
    return m;
}

Matrix44f ShadowService::CameraSliceProjLH(const Matrix44f& camProj,
                                           float            cameraNearZ,
                                           float            cameraFarZ,
                                           float            nearSplit,
                                           float            farSplit) const {
    // Re-project the camera's near/far plane into the [nearSplit,
    // farSplit] sub-range. The full-frame camera proj has rows that
    // depend on (n, f) — easiest path is to build a fresh proj for
    // the slice using the renderer's diag-FOV builder. We don't have
    // FOV in hand here, so back-derive it from the projection matrix:
    // for the SgCompat LH proj built by Camera::ProjectionLH,
    // camProj.data[0][0] = 1 / tan(fov_x/2) (after compensation).
    // We just reuse `camProj` and scale the z-range — the perspective
    // shape is unchanged, only near/far matter.
    //
    // Implementation: compute the new (n, f) and patch the 2x2 z
    // sub-matrix of camProj. For LH SgCompat:
    //   row2.z = f / (f - n)
    //   row3.z = -n*f / (f - n)
    Matrix44f m = camProj;
    const float n = std::max(cameraNearZ, nearSplit);
    const float f = std::min(cameraFarZ,  farSplit);
    if (f <= n) return m;
    m.data[2][2] =  f          / (f - n);
    m.data[3][2] = -n * f      / (f - n);
    return m;
}

void ShadowService::FrustumCornersWS(const Matrix44f& view, const Matrix44f& proj,
                                     Vector3f outCorners[8]) {
    // 8 NDC cube corners: x∈{-1,1}, y∈{-1,1}, z∈{0,1} (LH D3D).
    static const Vector3f kNdc[8] = {
        {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
        {-1.0f,  1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
        {-1.0f, -1.0f, 1.0f}, { 1.0f, -1.0f, 1.0f},
        {-1.0f,  1.0f, 1.0f}, { 1.0f,  1.0f, 1.0f},
    };
    Matrix44f viewProj = view * proj;
    Matrix44f invVP    = Matrix44f::inverse(viewProj);
    for (int i = 0; i < 8; ++i) {
        outCorners[i] = MulPoint(invVP, kNdc[i]);
    }
}

// ----------------------------------------------------------------------------
// Update — the per-frame entry point
// ----------------------------------------------------------------------------

void ShadowService::Update(const Matrix44f& cameraViewLH,
                           const Matrix44f& cameraProjLH,
                           float            cameraNearZ,
                           float            cameraFarZ,
                           const Vector3f&  lightDirWS,
                           const Vector3f&  sceneCenterWS,
                           float            sceneRadius) {
    if (!params_.enabled) return;
    EnsureTargets();
    if (!targetsValid_) return;

    lightView_ = BuildLightViewLH(sceneCenterWS, lightDirWS, params_.casterHeight);

    // ----- 1. Scene AABB in light space ------------------------------------
    //
    // The model viewer's scene is small (one centred actor at most), so
    // the CSM frustum should hug the scene rather than the full camera
    // frustum (which extends out to cameraFarZ ≈ 10000 — covering 100×
    // the model and producing ~10 world units per shadow-map texel).
    //
    // We compute a tight light-space AABB from the 8 corners of the
    // scene's world-space cube (centre ± sceneRadius). The per-cascade
    // ortho frustum then intersects the camera split with this AABB,
    // clamping cascades that try to grow past the scene.
    Vector3f sceneAabbMinLS = { 3.4e38f,  3.4e38f,  3.4e38f};
    Vector3f sceneAabbMaxLS = {-3.4e38f, -3.4e38f, -3.4e38f};
    {
        const float r = std::max(sceneRadius, 1.0f);
        const Vector3f sceneCornersWS[8] = {
            {sceneCenterWS.x - r, sceneCenterWS.y - r, sceneCenterWS.z - r},
            {sceneCenterWS.x + r, sceneCenterWS.y - r, sceneCenterWS.z - r},
            {sceneCenterWS.x - r, sceneCenterWS.y + r, sceneCenterWS.z - r},
            {sceneCenterWS.x + r, sceneCenterWS.y + r, sceneCenterWS.z - r},
            {sceneCenterWS.x - r, sceneCenterWS.y - r, sceneCenterWS.z + r},
            {sceneCenterWS.x + r, sceneCenterWS.y - r, sceneCenterWS.z + r},
            {sceneCenterWS.x - r, sceneCenterWS.y + r, sceneCenterWS.z + r},
            {sceneCenterWS.x + r, sceneCenterWS.y + r, sceneCenterWS.z + r},
        };
        for (const auto& p : sceneCornersWS) {
            const Vector3f ls = MulPoint(lightView_, p);
            sceneAabbMinLS.x = std::min(sceneAabbMinLS.x, ls.x);
            sceneAabbMaxLS.x = std::max(sceneAabbMaxLS.x, ls.x);
            sceneAabbMinLS.y = std::min(sceneAabbMinLS.y, ls.y);
            sceneAabbMaxLS.y = std::max(sceneAabbMaxLS.y, ls.y);
            sceneAabbMinLS.z = std::min(sceneAabbMinLS.z, ls.z);
            sceneAabbMaxLS.z = std::max(sceneAabbMaxLS.z, ls.z);
        }
    }

    // ----- 2. PSSM cascade splits in camera-z ------------------------------
    //
    //   For cascade i ∈ [0, N):
    //     u_i        = (i + 1) / N
    //     z_uniform  = zNear + (zFar - zNear) * u_i
    //     z_log      = zNear * pow(zFar / zNear, u_i)
    //     splits[i]  = lerp(z_uniform, z_log, lambdaSplit)
    //
    // splits[-1] is implicitly the camera near plane.
    //
    // zFar is clamped to a multiple of sceneRadius so the splits don't
    // explode out to the full camera farZ (which on the model viewer
    // is 10000, i.e. ~50× the actor extent — that produces unusable
    // shadow-map resolution after AABB-snap). Tunable via the
    // multiplier; 4× scene radius covers the actor's worst-pose
    // extent (raised weapons, splayed wings).
    const int   N      = std::clamp(params_.cascadeCount, 1, 3);
    const float zNear  = std::max(cameraNearZ, 1.0f);
    const float farCap = std::max(zNear + 1.0f, sceneRadius * 4.0f);
    const float zFar   = std::min(std::max(zNear + 1.0f, cameraFarZ), farCap);
    const float ratio  = zFar / zNear;
    float splits[4]    = { zNear, 0.0f, 0.0f, zFar };
    for (int i = 0; i < N - 1; ++i) {
        const float u  = static_cast<float>(i + 1) / static_cast<float>(N);
        const float zU = zNear + (zFar - zNear) * u;
        const float zL = zNear * std::pow(ratio, u);
        splits[i + 1]  = zU + (zL - zU) * params_.lambdaSplit;
    }
    splits[N] = zFar;

    // ----- 3. Per-cascade ortho VP -----------------------------------------
    for (int c = 0; c < N; ++c) {
        const float nSlice = splits[c];
        const float fSlice = splits[c + 1];

        // Slice's perspective sub-projection. Camera forms the full
        // frustum, the shadow projection only needs to cover the
        // pyramid between nSlice and fSlice.
        const Matrix44f sliceProj = CameraSliceProjLH(
            cameraProjLH, cameraNearZ, cameraFarZ, nSlice, fSlice);

        // 8 world-space corners of the frustum slice.
        Vector3f cornersWS[8];
        FrustumCornersWS(cameraViewLH, sliceProj, cornersWS);

        // Transform into light-view space and find the AABB.
        Vector3f minLS = { 3.4e38f,  3.4e38f,  3.4e38f};
        Vector3f maxLS = {-3.4e38f, -3.4e38f, -3.4e38f};
        for (int i = 0; i < 8; ++i) {
            const Vector3f p = MulPoint(lightView_, cornersWS[i]);
            minLS.x = std::min(minLS.x, p.x);  maxLS.x = std::max(maxLS.x, p.x);
            minLS.y = std::min(minLS.y, p.y);  maxLS.y = std::max(maxLS.y, p.y);
            minLS.z = std::min(minLS.z, p.z);  maxLS.z = std::max(maxLS.z, p.z);
        }

        // Intersect with the scene AABB. The XY clip tightens the
        // ortho frustum to scene extent (so shadow texels actually
        // land on the model rather than being smeared across an
        // empty 10000-unit camera-far envelope). The Z clip is
        // one-sided: keep the camera-frustum near (so shadow casters
        // BETWEEN the light and the model still write depth) but cap
        // the far at the scene's far edge in light space.
        minLS.x = std::max(minLS.x, sceneAabbMinLS.x);
        maxLS.x = std::min(maxLS.x, sceneAabbMaxLS.x);
        minLS.y = std::max(minLS.y, sceneAabbMinLS.y);
        maxLS.y = std::min(maxLS.y, sceneAabbMaxLS.y);
        // Light-Z grows AWAY from the light. The cascade's Z range
        // must contain every potential caster (anything between the
        // light and a receiver in this slice) AND every receiver in
        // this slice — anything outside this range gets frustum-
        // clipped at the rasterizer and never writes depth.
        //
        // For the closest (cascade 0) PSSM slice the projected
        // slice_maxZ is small, so the previous `std::min` clamp
        // pinned the cascade's far plane BEFORE the scene's far
        // extent — any caster past slice_maxZ vanished, leaving
        // cascade 0 entirely empty. Use the scene's full Z extent
        // (with a small padding either side) regardless of which
        // slice we're rendering: every cascade still gets its own
        // tight XY frustum from the slice corners, but Z spans the
        // whole scene so casters are never clipped out.
        minLS.z = std::min(minLS.z, sceneAabbMinLS.z - 50.0f);
        maxLS.z = std::max(maxLS.z, sceneAabbMaxLS.z + 50.0f);
        // Degenerate-AABB fallback: if the camera-frustum-vs-scene
        // intersection collapses on ANY axis, OrthoLHOffcenter would
        // divide by zero (or near-zero) and emit an Inf/NaN matrix.
        // The shader's `mul(cascade, worldPos4)` propagates that into
        // SV_POSITION and the rasterizer responds with undefined
        // behaviour — observed slowly accumulating into a TDR on the
        // INI-startup path where the camera's at the default pose
        // before any model has loaded. Falling back to the full scene
        // AABB on any axis collapse keeps the matrix finite (and
        // matches the existing Z-only fallback's intent).
        const bool degenerate =
            (maxLS.x <= minLS.x) ||
            (maxLS.y <= minLS.y) ||
            (maxLS.z <= minLS.z);
        if (degenerate) {
            minLS = sceneAabbMinLS;
            maxLS = sceneAabbMaxLS;
            minLS.z -= 50.0f;
            maxLS.z += 50.0f;
            // Final guard: even the scene AABB can be degenerate on a
            // freshly-constructed render (sceneRadius clamped to 1).
            // Pad each axis so the ortho proj is always non-zero.
            const float kMinExtent = 1.0f;
            if (maxLS.x - minLS.x < kMinExtent) {
                const float c = 0.5f * (minLS.x + maxLS.x);
                minLS.x = c - 0.5f * kMinExtent;
                maxLS.x = c + 0.5f * kMinExtent;
            }
            if (maxLS.y - minLS.y < kMinExtent) {
                const float c = 0.5f * (minLS.y + maxLS.y);
                minLS.y = c - 0.5f * kMinExtent;
                maxLS.y = c + 0.5f * kMinExtent;
            }
            if (maxLS.z - minLS.z < kMinExtent) {
                const float c = 0.5f * (minLS.z + maxLS.z);
                minLS.z = c - 0.5f * kMinExtent;
                maxLS.z = c + 0.5f * kMinExtent;
            }
        }

        // Texel-snap the XY bounds to prevent shimmer when the camera
        // pans. Mirrors preview.exe's `floorf(min/V)*V`,
        // `ceilf(max/V)*V` — engine `CLAMP_VALUE` literal couldn't be
        // recovered from the decompile, so we derive it from the
        // current cascade extent and resolution: one full texel.
        if (params_.texelSnap && cascades_[c].resolution > 0) {
            const float worldUnitsPerTexelX =
                (maxLS.x - minLS.x) / static_cast<float>(cascades_[c].resolution);
            const float worldUnitsPerTexelY =
                (maxLS.y - minLS.y) / static_cast<float>(cascades_[c].resolution);
            if (worldUnitsPerTexelX > 0.0f && worldUnitsPerTexelY > 0.0f) {
                minLS.x = std::floor(minLS.x / worldUnitsPerTexelX) * worldUnitsPerTexelX;
                maxLS.x = std::ceil (maxLS.x / worldUnitsPerTexelX) * worldUnitsPerTexelX;
                minLS.y = std::floor(minLS.y / worldUnitsPerTexelY) * worldUnitsPerTexelY;
                maxLS.y = std::ceil (maxLS.y / worldUnitsPerTexelY) * worldUnitsPerTexelY;
            }
        }

        // Off-centre LH ortho proj covering the AABB.
        const Matrix44f cascadeProj = OrthoLHOffcenter(
            minLS.x, maxLS.x, minLS.y, maxLS.y, minLS.z, maxLS.z);

        // Combined world → light-clip. The HD VS calls
        //     mul(cascade_n, mul(world, pos))
        // which in row-vector convention equals
        //     pos * world * cascade_n
        // → cascade_n = lightView * cascadeProj. Order matters
        // because the renderer's Matrix44f is row-major.
        cascades_[c].worldToClip = lightView_ * cascadeProj;
    }

    // Unused cascades get an out-of-frustum identity so the shader's
    // `isInShadowFrustum` rejects them and falls through to "fully lit".
    for (int c = N; c < 3; ++c) {
        cascades_[c].worldToClip = Matrix44f::identity();
    }
}

} // namespace WhiteoutDex::shadow
