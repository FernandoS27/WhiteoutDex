#include "bls_draw_helpers.h"
#include "coordinate_system.h"  // whiteout::transform_normal / transform_point

#include <cmath>

namespace WhiteoutDex::bls {

int BuildLightPalette(FrameInputs&                                     frame,
                      const std::vector<FrameState::LightState>&       activeLights,
                      const Matrix44f&                                 viewMatrix,
                      const BaselineLights&                            baseline) {
    int count = 0;

    // If the model has no authored-and-enabled lights, fall back to the
    // caller-supplied baseline (typically a camera-attached headlight with
    // a hand-tuned diffuse/ambient). Adding a baseline when authored lights
    // do exist oversaturates the SD shader's saturate(diff + amb) — the
    // engine follows the same rule so we mirror it.
    bool anyEnabled = false;
    for (const auto& L : activeLights) {
        if (L.enabled) { anyEnabled = true; break; }
    }
    if (!anyEnabled) {
        ShaderLight& sl = frame.lights[count++];
        sl.ambient  = { baseline.ambient.x,       baseline.ambient.y,       baseline.ambient.z,       0.0f };
        sl.diffuse  = { baseline.diffuse.x,       baseline.diffuse.y,       baseline.diffuse.z,       0.0f };
        sl.position = { baseline.dirToSourceVS.x, baseline.dirToSourceVS.y, baseline.dirToSourceVS.z, 0.0f };
    }

    for (const auto& L : activeLights) {
        if (!L.enabled) continue;
        if (count >= kMaxLights) break;
        ShaderLight& sl = frame.lights[count++];
        sl.ambient = { L.ambient.x, L.ambient.y, L.ambient.z, 0.0f };
        sl.diffuse = { L.diffuse.x, L.diffuse.y, L.diffuse.z, 0.0f };
        if (L.kind == FrameState::LightKind::Directional) {
            // Engine stores -normalize(m_dir) as the shader light vector;
            // transform_normal keeps the orientation in view space where
            // the shader's N·L dot product happens.
            Vector3f d = L.worldDir;
            float    n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (n > 1e-6f) { d.x /= n; d.y /= n; d.z /= n; }
            const Vector3f lv = whiteout::transform_normal(Vector3f{-d.x, -d.y, -d.z}, viewMatrix);
            sl.position = { lv.x, lv.y, lv.z, 0.0f };  // type=0 (directional)
        } else {
            // Omni + Ambient: place at world position in view space.
            const Vector3f p = whiteout::transform_point(L.worldPos, viewMatrix);
            sl.position = { p.x, p.y, p.z, 1.0f };     // type=1 (positional)
        }
    }

    for (int i = count; i < kMaxLights; ++i) frame.lights[i] = {};
    return count;
}

RenderState MakeSdMeshRenderState(const MatParams& mat, int activeLights, bool unlit) {
    RenderState rs;
    rs.shaderId        = GxShaderID::SD;
    rs.alphaMode       = static_cast<uint8_t>(mat.alpha);
    rs.numColors       = 1;
    rs.numTexCoords    = 1;
    rs.numWeights      = 0;                 // compute pass has already baked skinning
    rs.numLights       = static_cast<uint8_t>(activeLights);
    rs.fogEnabled      = false;
    rs.depthWrite      = mat.DepthWriteEnabled();
    rs.lightingEnabled = !unlit && activeLights > 0;
    return rs;
}

PsoRequest MakePsoRequest(const BlsProgram* program,
                          VertexLayoutKind  layout,
                          const MatParams&  mat,
                          PermuteIndices    perm,
                          bool              lhClipSpace) {
    PsoRequest req{};
    req.program     = program;
    req.vsIndex     = perm.vs;
    req.psIndex     = perm.ps;
    req.material    = mat;
    req.layout      = layout;
    req.lhClipSpace = lhClipSpace;
    return req;
}

} // namespace WhiteoutDex::bls
