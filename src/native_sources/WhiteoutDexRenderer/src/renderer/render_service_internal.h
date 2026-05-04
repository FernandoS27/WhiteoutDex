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

namespace WhiteoutDex { class SamplerAssetManager; }

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
    // UVAS channel selector (MDX Layer.CoordID). 0 (default) = channel 0
    // / `unskinnedVb`; 1 = channel 1 / `unskinnedVb1` (when baked); -1 =
    // SphereEnvMap (no shader-side UV synthesis yet — degrades to channel
    // 0). See reference_geoset_uv_channels.md.
    int      coordId             = 0;
};

// Returns the layer at `layerIndex` unpacked, or a default-constructed
// UnpackedLayer if `mat` is null or `layerIndex` is out of range. The
// previous inline form used `FILTER_NONE / 0 / 1.0f / -1` fallbacks —
// this function preserves those exactly.
UnpackedLayer UnpackLayer(const GPUMaterial* mat, int layerIndex);

// Per-actor view exposed to render passes (F6). Owns pointers into the actor's
// RenderModel + the per-actor world transform / visibility. Lifetime: built
// per-frame and stored stable in `CollectedRenderables::views` (reserved
// up-front so emplace_back never reallocates). Render passes never see
// `Actor`, only `RenderableView`.
struct RenderableView {
    const std::vector<GPUGeoset>*           geosets        = nullptr;
    const std::vector<GPUMaterial>*         materials      = nullptr;
    TextureAssetManager::ModelScope*        textures       = nullptr;
    const SkinningSystem*                   skinning       = nullptr;
    const std::vector<FrameState::LightState>*       activeLights   = nullptr;
    const std::vector<RenderModel::TexAnimPaletteEntry>* texAnimPalette = nullptr;
    Matrix44f                               worldTransform = Matrix44f::identity();
    float                                   parentVisibility = 1.0f;
    bool                                    hasLods        = false;
};

// Per-geoset sortable reference produced by the collect pass. Points into a
// stable `RenderableView` (held by CollectedRenderables); the geoset index
// addresses `view->geosets`.
struct GeosetRef {
    const RenderableView* view;
    int idx;
    int renderOrder;
    int priorityPlane;
    int geosetId;
};

// Output of CollectSortedRenderables. Views are reserved up-front so the
// raw pointers stored in `refs` remain stable for the frame.
struct CollectedRenderables {
    std::vector<RenderableView> views;
    std::vector<GeosetRef>      refs;
};

// Walks `models`, drops instances with parentVisibility <= 0.02, applies the
// per-model LOD filter (respecting hasLods and the always-draw sentinel
// 0xFFFFFFFF via RenderService::GeosetPassesLod), computes each layer's
// render-order bucket via RenderService::GetRenderOrder, and returns the
// result sorted by {renderOrder, priorityPlane, geosetId}.
//
// `selectedLod` is the caller's ComputeSelectedLod() result — kept as a
// parameter so this helper stays free-function and testable.
CollectedRenderables CollectSortedRenderables(
    const std::unordered_map<uint32_t, std::unique_ptr<Actor>>& models,
    int selectedLod);

// Binds the SD-mesh input for native VS skinning:
//   slot 0 = geo.unskinnedVb  (rest-pose Vertex, 48 B stride)  OR
//            geo.unskinnedVb1 if `coordId == 1` AND that VB exists
//   slot 1 = geo.boneVb       (BoneVertex, 8 B) -- iff bones are present
//   vsCB3  = geo.bonePaletteCb                   -- iff bones are present
// Plus the R32_UINT index buffer. Returns true when the bone stream
// was bound; caller then picks numWeights=4 + ParticleSDSkinned layout
// so the SD VS selects the FourBoneSkinning permute.
bool BindSdMeshGeometry(gfx::IGFXCommandList* cmd,
                        const GPUGeoset&      geo,
                        int                   coordId = 0);

// Resolves which slot-0 vertex buffer to bind for a given UV channel
// selector. Returns `geo.unskinnedVb1` only when the layer asked for
// channel 1 AND the template baked a sibling for it; falls back to
// `geo.unskinnedVb` (channel 0) for every other case (including
// SphereEnvMap layers, which want shader-side UV synthesis but we
// currently degrade to channel 0).
gfx::BufferHandle PickSlot0Vb(const GPUGeoset& geo, int coordId);

// Binds `textureId`'s GPU texture to pixel-stage slot `slot` with the
// matching wrap sampler resolved through `samplers`. Falls back to
// `defaultTex` when the id is unresolvable or the texture handle is invalid.
// SamplerAssetManager owns the wrap-flags → SamplerHandle mapping (bits
// masked against kSamplerWrapBitsMask internally) so this site no longer
// indexes a raw [4] array.
void BindLayerAlbedo(gfx::IGFXCommandList*       cmd,
                     TextureAssetManager::ModelScope* scope,
                     int                         textureId,
                     gfx::TextureHandle          defaultTex,
                     SamplerAssetManager&        samplers,
                     uint32_t                    slot = 0);

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

// Populate `frame.texMtx0` (layer UV matrix from the actor's texAnimPalette)
// and `frame.texMtx1` (always identity — WC3 MDX only uses one UV set).
// Replaces the eight-line palette-lookup block duplicated verbatim in
// GeosetPassBls and GeosetPassHd. Out-of-range / missing ids resolve to
// the BLS identity tex-matrix so the VS's `tex0 = texMtx0 * (u,v,1)`
// becomes a no-op.
void ApplyTexAnimPaletteToFrame(bls::FrameInputs&    frame,
                                const std::vector<RenderModel::TexAnimPaletteEntry>* palette,
                                int                  textureAnimationId);

} // namespace WhiteoutDex::render_detail
