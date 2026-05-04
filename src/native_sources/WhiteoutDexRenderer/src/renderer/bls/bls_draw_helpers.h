#pragma once
// ============================================================================
// BLS-adjacent draw-path helpers — fill BLS frame payloads and request
// objects from renderer-domain inputs. Kept in renderer/bls/ because every
// helper's input or output is a bls:: type, even though the callers are
// renderer free functions. None of this code touches the GFX command list.
// ============================================================================

#include "bls_frame.h"
#include "bls_permuter.h"
#include "bls_pso_builder.h"
#include "model_types.h"   // FrameState::LightState / LightKind
#include "render_target.h" // LightingMode
#include "types.h"

#include <vector>

namespace WhiteoutDex::bls {

// Fallback lighting when a model ships no authored lights. The three vectors
// mirror the per-path constants that used to be inlined at each render
// function (kBaselineAmbient/Diffuse/DirToSourceVS). Populating this from
// the caller keeps path-specific nuances — notably the sign of the Z axis,
// which flips between RH (SD) and LH (HD) view matrices — out of the helper.
struct BaselineLights {
    Vector3f ambient        = {0.0f, 0.0f, 0.0f};
    Vector3f diffuse        = {1.0f, 1.0f, 1.0f};
    // View-space direction pointing *from* the shaded fragment *to* the
    // light source. In RH view (forward = -Z) this is +Z; in LH (forward =
    // +Z) this is -Z.
    Vector3f dirToSourceVS  = {0.0f, 0.0f, 1.0f};
};

// Writes up to kMaxLights entries into `frame.lights[]` based on the
// chosen LightingMode:
//   Dynamic — baseline only when no authored light is enabled.
//   InGame  — baseline always; authored lights stack on top.
//   Glue    — baseline never; only authored lights (may be empty).
// Authored lights are transformed into view space; remaining slots are
// zero-filled. Returns the populated light count.
int BuildLightPalette(FrameInputs&                                      frame,
                      const std::vector<FrameState::LightState>&        activeLights,
                      const Matrix44f&                                  viewMatrix,
                      const BaselineLights&                             baseline,
                      LightingMode                                      mode);

// Canonical BLS (SD-in-SD-mode) mesh RenderState. Mirrors the inline fill
// that was duplicated across RenderGeosetsBls / RenderParticlesBls:
//   shaderId=SD, numColors=1, numTexCoords=1,
//   numWeights = hasBones ? 4 : 0,
//   fogEnabled=false, lightingEnabled=(!unlit && activeLights > 0).
// Callers pass hasBones=true only when the geoset has a populated
// boneVb + bonePaletteCb; in that case the VS selects the native
// FourBoneSkinning permute and skins from slot-1 BoneVertex data. When
// hasBones=false the shader picks the "no skinning" permute and slot 0
// must carry already-rigid geometry (unskinnedVb for static geosets,
// or billboard geometry for particles/ribbons).
RenderState MakeSdMeshRenderState(const MatParams& mat,
                                  int              activeLights,
                                  bool             unlit,
                                  bool             hasBones = false);

// Assembles a PsoRequest from the pieces every BLS draw call has in hand.
// rtvFormat / dsvFormat / topology keep their struct defaults so callers
// don't restate them; `lhClipSpace` is surfaced because HD vs SD needs
// separate PSO cache lines even when the rasterizer state matches.
PsoRequest MakePsoRequest(const BlsProgram* program,
                          VertexLayoutKind  layout,
                          const MatParams&  mat,
                          PermuteIndices    perm,
                          bool              lhClipSpace = false);

} // namespace WhiteoutDex::bls
