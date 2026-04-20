#pragma once
// ============================================================================
// WhiteoutDex Renderer — ModelInstance
// Encapsulates all per-model state: GPU resources, staged data, subsystems.
// The Renderer owns one or more ModelInstance objects.
// ============================================================================

#include "types.h"
#include "../gfx/gfx.h"
#include "animation.h"
#include "particle.h"
#include "ribbon.h"
#include "pe1_system.h"
#include "model_source.h"
#include <unordered_map>
#include <vector>
#include <memory>

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
};

struct StagedMaterialLayer {
    int filterMode          = 0;
    int textureId           = -1;
    float alpha             = 1.0f;
    int flags               = 0;
    int textureAnimationId  = -1;  // -1 = no tex-anim; else index into MDX TXAN table
    int shaderId            = 0;   // Layer::ShaderType: 0=SD, 1=HD, 2=SDOnHD, 24=Crystal
    // Reforged HD subtextures (-1 = unused, use default).
    int normalMapId         = -1;
    int ormMapId            = -1;
    int emissiveMapId       = -1;
    int teamColorMapId      = -1;
    // HD material knobs forwarded to the HD PS CB (CGxMatParams::PixelParams).
    float    emissiveGain    = 0.0f;
    float    fresnelOpacity  = 0.0f;
    float    fresnelTeamColor = 0.0f;
    Vector3f fresnelColor    = {0.0f, 0.0f, 0.0f};
};

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
};

// ============================================================================
// GPU resources (render thread only)
// ============================================================================

struct GPUGeoset {
    int geosetId       = -1;
    gfx::BufferHandle vb = gfx::BufferHandle::Invalid;
    gfx::BufferHandle ib = gfx::BufferHandle::Invalid;
    // Side-stream tangent buffer (ATTR7 in HD VS input layout). Bound to
    // vertex buffer slot 1 for HD draws when the source geoset had
    // tangents; left Invalid otherwise and the HD draw picks the
    // no-tangent permute so it doesn't read from slot 1.
    gfx::BufferHandle tangentVb = gfx::BufferHandle::Invalid;
    int indexCount      = 0;
    int vertexCount     = 0;
    int materialId      = -1;

    std::vector<Vertex> baseVertices;
    bool hasSkinning    = false;

    // GPU compute skinning resources (structured buffers with auto-SRV/UAV).
    // These feed the legacy Slang/SD path via skin.slang -- the HD path
    // skins in the VS directly (vs/hd.bls uses FourBoneSkinning), so it
    // binds the unskinned buffers instead and never touches skinnedBuf.
    gfx::BufferHandle baseVertBuf  = gfx::BufferHandle::Invalid;
    gfx::BufferHandle weightBuf    = gfx::BufferHandle::Invalid;
    gfx::BufferHandle skinnedBuf   = gfx::BufferHandle::Invalid;

    // HD VS-time skinning side-streams.
    //   unskinnedVb : rest-pose ATTR0..ATTR3 stream (slot 0). Dedicated
    //                 VB rather than reusing baseVertBuf so the SRV
    //                 state of the compute pipeline doesn't fight the
    //                 Vertex state we'd need for a draw.
    //   boneVb      : packs per-vertex bone weights (ATTR5, R8G8B8A8_UNORM)
    //                 and indices (ATTR6, R8G8B8A8_UINT) into 8 bytes
    //                 matching MeshVertexSDSkinned's slot-1 layout.
    gfx::BufferHandle unskinnedVb = gfx::BufferHandle::Invalid;
    gfx::BufferHandle boneVb      = gfx::BufferHandle::Invalid;

    float geosetAlpha   = 1.0f;
    Vector3f geosetColor = {1,1,1};
    Matrix44f worldMatrix = Matrix44f::identity();
    int priorityPlane   = 0;

    void Release(gfx::IGFXDevice& gfx) {
        gfx.Destroy(vb);  gfx.Destroy(ib);
        gfx.Destroy(tangentVb);
        gfx.Destroy(baseVertBuf);
        gfx.Destroy(weightBuf);
        gfx.Destroy(skinnedBuf);
        gfx.Destroy(unskinnedVb);
        gfx.Destroy(boneVb);
        vb = gfx::BufferHandle::Invalid;
        ib = gfx::BufferHandle::Invalid;
        tangentVb = gfx::BufferHandle::Invalid;
        baseVertBuf = gfx::BufferHandle::Invalid;
        weightBuf = gfx::BufferHandle::Invalid;
        skinnedBuf = gfx::BufferHandle::Invalid;
        unskinnedVb = gfx::BufferHandle::Invalid;
        boneVb = gfx::BufferHandle::Invalid;
        indexCount = 0; vertexCount = 0;
        baseVertices.clear(); baseVertices.shrink_to_fit();
    }
};

struct GPUTexture {
    gfx::TextureHandle tex = gfx::TextureHandle::Invalid;
    uint32_t wrapFlags = 0x3;   // bit 0 = WrapWidth (U), bit 1 = WrapHeight (V)

    void Release(gfx::IGFXDevice& gfx) {
        gfx.Destroy(tex);
        tex = gfx::TextureHandle::Invalid;
    }
};

struct GPUMaterial {
    StagedMaterial cpu;
};

// ============================================================================
// Collision shape (per-model, rendered as wireframe)
// ============================================================================
struct CollisionShape {
    int type = 0;            // 0=box, 1=sphere
    Vector3f vmin = {0,0,0};
    Vector3f vmax = {0,0,0};
    float radius = 0;
    Matrix44f transform = Matrix44f::identity();
};

// ============================================================================
// Per-layer texture animation data
// ============================================================================
struct TexAnimData {
    float uOff=0, vOff=0, uTile=1, vTile=1, rotation=0;
};

// ============================================================================
// ModelInstance — all state for a single renderable model
// ============================================================================
struct ModelInstance {
    uint32_t handle = 0;
    bool isFocus = false;

    // World transform for the entire model instance
    // (identity for focus model, per-particle transform for PE1 children)
    Matrix44f worldTransform = Matrix44f::identity();

    // ---- Staged data (CPU side, written by API thread under dataMutex_) ----
    std::unordered_map<int, StagedGeoset>   stagedGeosets;
    std::unordered_map<int, StagedMaterial> stagedMaterials;
    std::unordered_map<int, StagedTexture>  stagedTextures;
    bool stagedDirty = false;
    bool stagedClear = false;

    // ---- GPU resources (render thread only) ----
    std::vector<GPUGeoset>              gpuGeosets;
    std::unordered_map<int, GPUTexture> gpuTextures;
    std::vector<GPUMaterial>            gpuMaterials;

    // ---- Skinning ----
    SkinningSystem skinning;
    bool skinDirty = false;
    std::vector<uint32_t> billboardFlags;  // per-node billboard flags
    std::vector<Vector3f> nodePivots;     // per-node rest pivots (for billboard rotation center)

    // GPU node palette (StructuredBuffer of offset matrices, one per model)
    gfx::BufferHandle nodePalette = gfx::BufferHandle::Invalid;

    // Per-frame bone palette CB consumed by vs/hd.bls (declared as
    // `ConstantBuffer<BonePalette> vsCB3 : register(b3)` in
    // wc3_shaders/types/cb_structs.slang). Sized for 256 bones worth of
    // float4x3 offset matrices (12288 B -- see bls_cb_layout.h
    // BonePaletteCb). Only populated when the model has a skeleton.
    gfx::BufferHandle bonePaletteCb = gfx::BufferHandle::Invalid;

    // ---- Particle system ----
    // PE2 particles are owned by RenderService::particleService_, keyed on
    // the model's handle. No per-instance state lives here any more.

    // ---- Ribbon system ----
    RibbonSystem ribbons;
    gfx::BufferHandle ribbonVB     = gfx::BufferHandle::Invalid;
    int               ribbonVBSize = 0;

    // ---- Collision shapes ----
    std::vector<CollisionShape> collisionShapes;

    // ---- Per-layer texture animation (updated per frame) ----
    // Legacy Slang-path cache, keyed by materialId * 1000 + layerIndex.
    std::unordered_map<int, TexAnimData> matTexAnim;

    // BLS-path palette: one 2x4 UV matrix per MDX textureAnimationId.
    // Each row is 4 floats; shader reads .xyw. Indexed densely by id --
    // layers look it up via their textureAnimationId. Resized by
    // ApplyLayerStates; missing slots are left as identity.
    struct TexAnimPaletteEntry { float row0[4]; float row1[4]; };
    std::vector<TexAnimPaletteEntry> texAnimPalette;

    // Evaluated scene lights from the last frame -- copied from FrameState.
    // The mesh draw path picks up to 8 of these + transforms into view space
    // at CB upload time. See docs/BLS_ShaderABI.md.
    std::vector<FrameState::LightState> activeLights;

    // ---- Replaceable texture map (for team color) ----
    // textureId → replaceableId (1=TeamColor, 2=TeamGlow)
    std::unordered_map<int, int> replaceableTexMap;

    // ---- Attachments with child models ----
    struct AttachmentSlot {
        AttachmentConfig config;
        uint32_t childModelHandle = 0;  // 0 = not yet loaded
        bool loaded = false;
        bool wasVisible = false;        // tracks first-visible for animation start
    };
    std::vector<AttachmentSlot> attachmentSlots;

    // Visibility multiplier driven by the parent model when this instance is
    // hosted as an attachment child. 1 = fully visible, 0 = fully hidden.
    // Authoritative source: only the parent's ApplyFrameState writes this.
    float parentVisibility = 1.0f;

    // ---- PE1 (model particle emitter) ----
    PE1System pe1;
    int pe1Depth = 0;           // recursion depth (0 = root model)
    bool isPE1Child = false;    // true if spawned by a PE1 particle

    // For PE1 children: adapter for animation evaluation (IModelSource)
    std::shared_ptr<IModelSource> pe1Adapter;
    int pe1BirthTimeMs = 0;
    int pe1SequenceIdx = 0;

    // Release all GPU resources
    void ReleaseGPU(gfx::IGFXDevice& gfx) {
        for (auto& g : gpuGeosets) g.Release(gfx);
        gpuGeosets.clear();
        for (auto& [id, t] : gpuTextures) t.Release(gfx);
        gpuTextures.clear();
        gpuMaterials.clear();
        gfx.Destroy(nodePalette); nodePalette = gfx::BufferHandle::Invalid;
        gfx.Destroy(bonePaletteCb); bonePaletteCb = gfx::BufferHandle::Invalid;
        gfx.Destroy(ribbonVB); ribbonVB = gfx::BufferHandle::Invalid; ribbonVBSize = 0;
    }
};

} // namespace WhiteoutDex
