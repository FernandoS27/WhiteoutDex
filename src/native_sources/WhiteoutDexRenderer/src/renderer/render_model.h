#pragma once
// ============================================================================
// WhiteoutDex Renderer — RenderModel
//
// The per-actor renderable + simulation state. Carved out of `Actor` in
// Phase 4 to give the GPU + simulation cluster its own type and a single
// concern ("the per-actor renderable data").
//
// Phase 4 implementation note: `Actor` publicly inherits from `RenderModel`
// to avoid churning ~150 call sites that read members directly. The boundary
// is real at the type level (RenderModel can be passed / stored as its own
// thing, methods grouped here are obviously single-concern), and the call
// sites can migrate to `actor.Render()` (exposed on Actor) at the pace each
// subsystem refactors. A follow-up cleanup will convert to composition
// (`std::unique_ptr<RenderModel> renderModel_`) once the migration is far
// enough along.
//
// All the staged-CPU + GPU + sim helper types (StagedGeoset, StagedMaterial,
// StagedTexture, GPUGeoset, GPUMaterial, CollisionShape, TexAnimData,
// PE2State) live in this header — they are RenderModel's data, not Actor's.
// ============================================================================

#include "../gfx/gfx.h"
#include "animation.h"
#include "model_types.h"
#include "particle.h"
#include "pe1_system.h"
#include "ribbon.h"
#include "texture_asset_manager.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace WhiteoutDex {

// ============================================================================
// Staged data (CPU side — written by API thread, read by render thread)
// ============================================================================

struct StagedTexture {
    // All mip levels packed tightly in mip0 → mipN-1 order (the byte
    // layout D3D12's GetCopyableFootprints expects). Source format is
    // preserved so BC3/BC5/BC7 normal maps reach the sampler without
    // a CPU decode round-trip — see TextureData::format comment.
    std::vector<uint8_t> pixels;
    gfx::Format format = gfx::Format::R8G8B8A8_UNORM;
    int width  = 0;
    int height = 0;
    int mipLevels = 1;
    int replaceableId = 0;
    uint32_t wrapFlags = 0x3;   // bit 0 = WrapWidth (U), bit 1 = WrapHeight (V)

    // Mirror of TextureData::sharedKey carried across the staging boundary.
    // Empty = per-model owned upload; non-empty = key into the shared
    // cross-model GPU texture cache in TextureAssetManager.
    std::string sharedKey;
};

// StagedMaterialLayer is field-identical to MaterialLayerData (the adapter-side
// struct from model_types.h). Carrying two separate types just forced every
// load site to copy member-by-member; aliasing collapses each of those into a
// single struct assignment.
using StagedMaterialLayer = MaterialLayerData;

struct StagedMaterial {
    std::vector<StagedMaterialLayer> layers;
    int priorityPlane = 0;
    int sortOrder     = 0;
};

struct StagedGeoset {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    // Per-vertex tangent frame for the HD path (ATTR7 in wc3_shaders
    // VSInput). Empty when the MDX geoset had no tangents -- the HD draw
    // then falls back to the no-tangent permute.
    std::vector<Vector4f> tangents;
    int materialId = -1;
    uint32_t lod = 0; // 0xFFFFFFFF = always-render sentinel (Previewd's -1)
};

// ============================================================================
// GPU resources (render thread only)
// ============================================================================

struct GPUGeoset {
    int geosetId       = -1;
    gfx::BufferHandle ib = gfx::BufferHandle::Invalid;
    // Slot-0 vertex stream (PNCT0 48 B, rest pose). Both SD and HD VS
    // consume this; FourBoneSkinning happens in the VS when a bone
    // stream is bound on slot 1 and vsCB3 is populated.
    gfx::BufferHandle unskinnedVb = gfx::BufferHandle::Invalid;
    // Side-stream tangent buffer (ATTR7). Bound to slot 1 for HD draws
    // whose source geoset authored tangents; left Invalid otherwise so
    // the HD VS picks the no-tangent permute.
    gfx::BufferHandle tangentVb = gfx::BufferHandle::Invalid;
    // Per-vertex bone weights (ATTR5, R8G8B8A8_UNORM) + indices
    // (ATTR6, R8G8B8A8_UINT, LOCAL slots into this geoset's palette),
    // 8 bytes per vertex.
    gfx::BufferHandle boneVb      = gfx::BufferHandle::Invalid;
    // Per-geoset bone palette CB (vsCB3). Sized for up to kMaxBones (256)
    // ShaderBone entries — matches Previewd's usedBonesPerPrimitive.
    // Only valid when boneVb is valid; rebuilt each frame from
    // SkinningSystem's global offsetMatrices via the geoset's subset list.
    gfx::BufferHandle bonePaletteCb = gfx::BufferHandle::Invalid;
    int indexCount      = 0;
    int vertexCount     = 0;
    int materialId      = -1;
    uint32_t lod        = 0; // Previewd: 0..3 or 0xFFFFFFFF (always render)

    bool hasSkinning    = false;

    float geosetAlpha   = 1.0f;
    Vector3f geosetColor = {1,1,1};
    Matrix44f worldMatrix = Matrix44f::identity();
    int priorityPlane   = 0;

    // `freeSharedBuffers` controls whether we destroy ib/unskinnedVb/
    // tangentVb/boneVb. Instances that borrow geometry from a ModelTemplate
    // pass false here — the template owns those buffers and frees them in its
    // own ReleaseGPU. bonePaletteCb is always per-instance and always freed.
    void Release(gfx::IGFXDevice& gfx, bool freeSharedBuffers = true) {
        if (freeSharedBuffers) {
            gfx.Destroy(ib);
            gfx.Destroy(unskinnedVb);
            gfx.Destroy(tangentVb);
            gfx.Destroy(boneVb);
        }
        gfx.Destroy(bonePaletteCb);
        ib = gfx::BufferHandle::Invalid;
        unskinnedVb = gfx::BufferHandle::Invalid;
        tangentVb = gfx::BufferHandle::Invalid;
        boneVb = gfx::BufferHandle::Invalid;
        bonePaletteCb = gfx::BufferHandle::Invalid;
        indexCount = 0; vertexCount = 0;
    }
};

// GPUTexture has been folded into TextureAssetManager::ModelScope::Entry.
// The scope owns the gfx::TextureHandle plus its wrapFlags and frees both
// when the model is unloaded; bind sites use textures->Get(id) /
// textures->WrapFlags(id) instead of touching a GPUTexture struct directly.

struct GPUMaterial {
    StagedMaterial cpu;
};

// ============================================================================
// Collision shape (per-model, rendered as wireframe)
// ============================================================================
struct CollisionShape {
    int type = 0;            // Previewd: 0=box, 1=cylinder, 2=sphere, 3=plane
    Vector3f vmin = {0,0,0}; // extent[0] — min for box, center for sphere, endpoint A for cylinder
    Vector3f vmax = {0,0,0}; // extent[1] — max for box, endpoint B for cylinder
    float radius = 0;
    Vector3f pivot = {0,0,0}; // Bind-pose world pivot; geoset corners sit at pivot + extent
    Matrix44f transform = Matrix44f::identity(); // skinning delta (identity at bind)
};

// ============================================================================
// Per-layer texture animation data
// ============================================================================
struct TexAnimData {
    float uOff=0, vOff=0, uTile=1, vTile=1, rotation=0;
};

struct PE2State {
    float lastEmissionRate = 0.0f;
    bool  emissionValid    = false;
};

// ============================================================================
// RenderModel — the per-actor GPU + sim state cluster.
// ============================================================================

struct RenderModel {
    // ---- Staged data (CPU side, written by API thread under dataMutex_) ----
    std::unordered_map<int, StagedGeoset>   stagedGeosets;
    std::unordered_map<int, StagedMaterial> stagedMaterials;
    std::unordered_map<int, StagedTexture>  stagedTextures;
    bool stagedDirty = false;
    bool stagedClear = false;

    // ---- GPU resources (render thread only) ----
    std::vector<GPUGeoset>                              gpuGeosets;
    std::unique_ptr<TextureAssetManager::ModelScope>    textures;
    std::vector<GPUMaterial>                            gpuMaterials;

    // ---- Skinning ----
    SkinningSystem        skinning;
    bool                  skinDirty = false;
    std::vector<uint32_t> billboardFlags;
    std::vector<Vector3f> nodePivots;
    std::vector<int>      nodeParents;

    // ---- Particle / ribbon / collision sim ----
    std::vector<PE2State>       pe2State;
    RibbonSystem                ribbons;
    gfx::BufferHandle           ribbonVB     = gfx::BufferHandle::Invalid;
    int                         ribbonVBSize = 0;
    PE1System                   pe1;
    std::vector<CollisionShape> collisionShapes;

    // ---- Per-layer texture animation (updated per frame) ----
    std::unordered_map<int, TexAnimData> matTexAnim;

    struct TexAnimPaletteEntry { float row0[4]; float row1[4]; };
    std::vector<TexAnimPaletteEntry> texAnimPalette;

    // ---- Evaluated scene lights from the last frame ----
    std::vector<FrameState::LightState> activeLights;

    // ---- LOD ----
    bool hasLods = false;

    // ---- Per-frame state application ----
    // These methods pull animated state out of a FrameState and write it onto
    // this RenderModel's mutable per-frame data. Pure per-actor — no scene/
    // gfx access. The remaining Apply* helpers (bones, particles, attachments)
    // need external context (camera / particleService_ / cross-actor lookups)
    // and stay on RenderService for now.
    void ApplyGeosetStates(const FrameState& state);
    void ApplyLayerStates(const FrameState& state);
    void ApplyRibbonFrameStates(const FrameState& state);
    void ApplyPE1FrameStates(const FrameState& state);
};

} // namespace WhiteoutDex
