#pragma once
// ============================================================================
// ShadowParams — host-tunable knobs for the cascaded-shadow-map service.
//
// Mirrors `WorldShadowParams` from preview.exe (`?WorldShadowGetParams` @
// 0x7ff609b0fa80). The engine ships per-scene-type configurations
// (Game / Cinematic / Portrait); the model viewer is closest to
// `Portrait` — close-range, small set of shadow casters. Single
// 1024-square cascade is plenty for that use case; the user can opt
// up via the Settings combo.
// ============================================================================

#include <cstdint>

namespace WhiteoutDex::shadow {

struct ShadowParams {
    // Number of active cascades, 1-3. The shader's
    // `sdSampleShadowCascades` selector iterates cascades in order
    // until the first one whose frustum contains the pixel; cascade 0
    // is the highest-resolution / closest-to-camera split.
    int   cascadeCount       = 1;

    // Per-cascade square depth-target resolution. 1024 is a good
    // model-viewer default — at this size a single 32-bit depth
    // cascade is 4 MiB, the static-sampler PCF tap stays cheap, and
    // shadow acne is fully tameable through the bias knobs below.
    int   cascadeResolution  = 1024;

    // The light's anchor offset along -lightDir from the scene
    // centre — `lightPos = sceneCenter - lightDir * casterHeight`.
    // Needs to be > the maximum extent of any shadow caster on the
    // up-axis so the orthographic frustum covers the whole model
    // even when arms are raised / weapons extended.
    float casterHeight       = 200.0f;

    // PSSM split lambda. 0 = uniform splits across the camera frustum
    // (cascades grow linearly), 1 = logarithmic (cascade 0 is much
    // tighter than cascade 2 — better for distant terrain). 0.5 is the
    // common practical default.
    float lambdaSplit        = 0.5f;

    // Rasterizer depth-bias knobs. Mapped 1:1 onto
    // D3D12_RASTERIZER_DESC.DepthBias / SlopeScaledDepthBias /
    // DepthBiasClamp.
    //
    // Currently zeroed — RenderDoc capture confirmed that with
    // `slopeScaledBias=1.5` + `depthBiasClamp=0` (unclamped) the
    // tightest cascade's tilted surfaces saw enough bias to push
    // fragment depth past 1.0, failing the LessEqual depth test
    // and leaving the cascade map empty. Re-tune by raising
    // depthBias (in ULPs of D32_FLOAT, so a small value buys a
    // usable nudge) and clamping slopeScaledBias via
    // depthBiasClamp once shadows are confirmed visible — the
    // clamp is what keeps the slope contribution from running
    // away on tight cascades.
    int   depthBias          = 0;
    float slopeScaledBias    = 0.0f;
    float depthBiasClamp     = 0.0f;

    // Ortho-frustum texel snap unit (light-space units per shadow-
    // map texel). Set non-zero so a panning camera doesn't slide
    // texels under the AABB and produce visible flicker. The actual
    // value is computed per-frame as
    // `cascadeWidth / cascadeResolution`; this knob lets a host
    // override it when chasing a specific snap behaviour.
    bool  texelSnap          = true;

    // Master toggle. When false, the service still constructs but
    // skips the per-frame Update + the shadow render pass. Mirrors
    // the existing `BLS RenderState::shadows` flag — flips the
    // HAS_SHADOWS perm on the HD/SD-on-HD VS+PS.
    bool  enabled            = false;
};

} // namespace WhiteoutDex::shadow
