#pragma once
// ============================================================================
// Render-service internal helpers — shared across RenderGeosetsBls/Hd/legacy
// render paths. Lives outside the RenderService class so the three paths can
// share the de-duplicated implementation without exposing class internals.
// ============================================================================

#include "gfx/gfx.h"
#include "model_instance.h"
#include "types.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace WhiteoutDex::bls { struct FrameInputs; }

namespace WhiteoutDex::render_detail {

// Snapshot of one StagedMaterialLayer plus the defaults used when a geoset
// has no material or a layer index is out of range. Extraction was previously
// inlined at three render paths with slightly different field subsets;
// consolidating them here means every path sees the same defaults and never
// drifts. Populating unused fields (e.g. normalMapId in the SD path) is
// harmless — the SD draw loop simply ignores them.
struct UnpackedLayer {
    int      filterMode          = FILTER_NONE;
    int      flags               = 0;
    float    alpha               = 1.0f;
    int      textureId           = -1;
    int      textureAnimationId  = -1;
    int      shaderId            = 0;
    int      normalMapId         = -1;
    int      ormMapId            = -1;
    int      emissiveMapId       = -1;
    int      teamColorMapId      = -1;
    float    emissiveGain        = 0.0f;
    float    fresnelOpacity      = 0.0f;
    float    fresnelTeamColor    = 0.0f;
    Vector3f fresnelColor        = {0.0f, 0.0f, 0.0f};
};

// Returns the layer at `layerIndex` unpacked, or a default-constructed
// UnpackedLayer if `mat` is null or `layerIndex` is out of range. The
// previous inline form used `FILTER_NONE / 0 / 1.0f / -1` fallbacks —
// this function preserves those exactly.
UnpackedLayer UnpackLayer(const GPUMaterial* mat, int layerIndex);

// Per-geoset sortable reference produced by the collect pass. Members match
// the previously-duplicated locally-declared GeosetRef struct in each render
// path so the rest of the draw loops can switch to this type unchanged.
struct GeosetRef {
    ModelInstance* mi;
    int idx;
    int renderOrder;
    int priorityPlane;
    int geosetId;
};

// Walks `models`, drops instances with parentVisibility <= 0.02, applies the
// per-model LOD filter (respecting hasLods and the always-draw sentinel
// 0xFFFFFFFF via RenderService::GeosetPassesLod), computes each layer's
// render-order bucket via RenderService::GetRenderOrder, and returns the
// result sorted by {renderOrder, priorityPlane, geosetId}.
//
// `selectedLod` is the caller's ComputeSelectedLod() result — kept as a
// parameter so this helper stays free-function and testable.
std::vector<GeosetRef> CollectSortedGeosetRefs(
    const std::unordered_map<uint32_t, std::unique_ptr<ModelInstance>>& models,
    int selectedLod);

// Binds the SD-mesh input layout: slot 0 = geo.vb at Vertex stride, plus
// the R32_UINT index buffer. The compute-skin pass has already written
// skinned positions into geo.vb, so no bone stream is needed on this path.
void BindSdMeshGeometry(gfx::IGFXCommandList* cmd, const GPUGeoset& geo);

// Binds `textureId`'s GPU texture to pixel-stage slot `slot` with the
// matching wrap sampler from `samplerWrap[0..3]`. Falls back to
// `defaultTex` when the id is unresolvable or the texture handle is invalid,
// masking the wrap flags to `kWrapFlagsMask` (2 bits) like the old inline
// form. Handles the "no texture + default sampler" case so callers never
// bind an invalid SRV.
void BindLayerAlbedo(gfx::IGFXCommandList*    cmd,
                     ModelInstance&           mi,
                     int                      textureId,
                     gfx::TextureHandle       defaultTex,
                     const gfx::SamplerHandle (&samplerWrap)[4],
                     uint32_t                 slot = 0);

// Legacy (Slang) CBPerFrame is filled at ten sites across the five
// non-BLS render paths (particles, ribbons, collisions, light markers,
// legacy geosets, ViewCube + frame setup). Before this struct each site
// inlined a near-identical Map/fill/Unmap stanza and drifted on the
// rarely-touched fields (`extraParams`/`texAnimParams`/`materialFlags`).
//
// Defaults mirror the "neutral scene" values used by RenderFrame() —
// identity world, no tex anim, unshaded flags zeroed. Callers only
// restate the fields their path actually varies (view/proj + lighting
// for most, plus the layer-specific fields for the legacy geoset path).
struct CbPerFrameDesc {
    Matrix44f world         = Matrix44f::identity();
    Matrix44f view          = Matrix44f::identity();
    Matrix44f projection    = Matrix44f::identity();
    Vector4f  lightDir      = {0.0f, 0.0f, 0.0f, 0.0f};
    Vector4f  lightColor    = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector4f  ambientColor  = {1.0f, 1.0f, 1.0f, 0.0f};
    Vector4f  extraParams   = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector4f  texAnimParams = {0.0f, 0.0f, 1.0f, 1.0f};
    Vector4f  materialFlags = {0.0f, 0.0f, 0.0f, 0.0f};
};

// Map `cb`, transpose all three matrices into HLSL row-major, copy the
// remaining scalar channels verbatim, unmap. No-op when the handle is
// invalid so the helper can be called from still-initialising paths.
void WriteCbPerFrame(gfx::IGFXDevice*     gfx,
                     gfx::BufferHandle    cb,
                     const CbPerFrameDesc& d);

// Normalize a direction vector and pack it into the {x,y,z,0} Vector4f
// shape that CBPerFrame::lightDir expects. Small convenience that keeps
// the call sites to a single line.
Vector4f NormalizedLightDir4(const Vector4f& dir);

// Populate `frame.texMtx0` (layer UV matrix from the mi texAnimPalette)
// and `frame.texMtx1` (always identity — WC3 MDX only uses one UV set).
// Replaces the eight-line palette-lookup block duplicated verbatim in
// GeosetPassBls and GeosetPassHd. Out-of-range / missing ids resolve to
// the BLS identity tex-matrix so the VS's `tex0 = texMtx0 * (u,v,1)`
// becomes a no-op.
void ApplyTexAnimPaletteToFrame(bls::FrameInputs&    frame,
                                const ModelInstance& mi,
                                int                  textureAnimationId);

} // namespace WhiteoutDex::render_detail
