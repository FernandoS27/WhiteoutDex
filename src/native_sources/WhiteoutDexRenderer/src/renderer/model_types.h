#pragma once
// ============================================================================
// WhiteoutDex Renderer — Common Model Types
// Strongly-typed intermediate structs for the IModelSource adapter pattern.
// Neither Max SDK nor WhiteoutLib types appear here.
// ============================================================================

#include "types.h"
#include "coordinate_system.h"
#include "particle.h"
#include "ribbon.h"
#include "animation.h"
#include "../gfx/gfx_types.h"
#include <vector>
#include <string>
#include <functional>

namespace WhiteoutDex {

// Camera preset for the selector combo. One entry per MDX `CAMS`
// chunk; "Free Camera" uses the orbital state instead.
struct CameraPreset {
    std::wstring name;
    bool isLive = false;

    // Absolute pose + projection from the MDX.
    Vector3f position{0.f, 0.f, 0.f};
    Vector3f target  {0.f, 0.f, 0.f};
    float    fovDiagonal = 0.95f;
    float    zNear       = 1.0f;
    float    zFar        = 10000.0f;
    float    staticRoll  = 0.0f;

    // Legacy pitch/yaw/distance readouts for UI tooltips.
    float pitch    = 0.0f;
    float yaw      = 0.0f;
    float distance = 100.0f;

    // Populated when the MDX camera has position/target/rotation
    // tracks. Invoked per-frame while active.
    std::function<void(Vector3f& pos, Vector3f& target,
                       float& roll, int timeMs,
                       int seqStart, int seqEnd)> animator;
};

// ============================================================================
// Attachment configuration — static data, set once
// ============================================================================
struct AttachmentConfig {
    int attachmentId = 0;
    std::string modelPath;   // path to attached model (empty = no model)
};

// ============================================================================
// PE1 (Model Particle Emitter) Configuration — static data, set once
// ============================================================================
struct PE1EmitterConfig {
    std::string modelPath;     // Path to spawned MDX model file
    float lifespan = 1.0f;    // Particle lifetime in seconds
    float scale    = 1.0f;    // Uniform model scale
};

// ============================================================================
// FilterMode enum (matches Magos Constants.h / WhiteoutDex IO)
// ============================================================================
enum FilterMode {
    FILTER_NONE        = 0,
    FILTER_TRANSPARENT = 1,
    FILTER_BLEND       = 2,
    FILTER_ADDITIVE    = 3,
    FILTER_ADD_ALPHA   = 4,
    FILTER_MODULATE    = 5,
    FILTER_MODULATE_2X = 6,
};

/// Map a raw integer (0-6) to a FilterMode value, clamped to valid range.
/// Works for MDX Layer::FilterMode, Wc3Material (after subtracting 1), and ribbons.
inline int MapFilterMode(int raw) {
    if (raw < 0) return FILTER_NONE;
    if (raw > 6) return FILTER_MODULATE_2X;
    return raw;
}

/// Map a Wc3Particles2 blend mode (0-4) to the renderer FilterMode.
/// MDX PE2 blendMode values (per the format spec, confirmed against
/// Previewd's ILoadParticleEmitters2 at 0x14049e71c): 0=Blend, 1=Add,
/// 2=Modulate, 3=Modulate2X, 4=AlphaKey. Slot 4 must map to
/// FILTER_TRANSPARENT (=AlphaKey), NOT FILTER_ADD_ALPHA — the latter is a
/// mesh-layer filter with no PE2 counterpart, and folding AlphaKey into
/// it would route alpha-tested particles into the additive blend path.
inline int MapPE2BlendMode(int blendMode) {
    static constexpr int table[] = {
        FILTER_BLEND, FILTER_ADDITIVE, FILTER_MODULATE, FILTER_MODULATE_2X, FILTER_TRANSPARENT
    };
    if (blendMode >= 0 && blendMode < 5) return table[blendMode];
    return FILTER_BLEND; // fallback
}

// Material flags (bitfield)
enum MaterialFlags {
    MAT_TWO_SIDED    = 1,
    MAT_UNSHADED     = 2,
    MAT_UNFOGGED     = 4,
    MAT_NO_DEPTH_TEST = 8,
    MAT_NO_DEPTH_SET  = 16,
    MAT_CONSTANT_COLOR = 32,
};

struct MeshData {
    int geosetId;
    int materialId;
    // MDX Reforged LOD level (0..3). Raw u32 from the file —
    // 0xFFFFFFFF means "always render regardless of selected LOD"
    // (see Previewd AddGeosetsToScene @0x140306750). Classic/pre-v900
    // models have no LOD data; the adapter leaves this 0.
    uint32_t lod = 0;
    std::vector<Vector3f> positions;
    std::vector<Vector3f> normals;
    std::vector<Vector2f> uvs;
    // Per-vertex tangent frame. .xyz = world-space tangent direction,
    // .w = handedness sign for bitangent reconstruction (see
    // tangentToWorld in wc3_shaders/math/normal.slang). Empty when the
    // source MDX geoset has no tangents authored; HD draws then fall
    // back to the no-tangent permute which loses normal-map detail.
    std::vector<Vector4f> tangents;
    std::vector<uint32_t>  indices;
};

struct TextureData {
    int textureId;
    int replaceableId;
    // All mip levels packed tightly in mip0 → mipN-1 order (the byte
    // layout D3D12_PLACED_SUBRESOURCE_FOOTPRINT expects). Source
    // format preserved end-to-end — BC3/BC5/BC7 normal maps must
    // reach the sampler in Blizzard's packed encoding because
    // hd_ps.slang::decodeNormalMap reconstructs nx via the R*A
    // product; a CPU decode to RGBA8 breaks that.
    std::vector<uint8_t> pixels;
    gfx::Format format = gfx::Format::R8G8B8A8_UNORM;
    int width, height;
    int mipLevels = 1;
    uint32_t wrapFlags = 0x3;   // bit 0 = WrapWidth (U), bit 1 = WrapHeight (V); default = wrap both
};

struct MaterialLayerData {
    int filterMode;
    int textureId;
    float alpha;
    int flags;  // MAT_TWO_SIDED, MAT_UNSHADED, etc. (from FilterMode/MaterialFlags enums)
    int textureAnimationId = -1;  // -1 = none; else index into model's TXAN table
    // MDX layer shader id (Layer::ShaderType). 0 = SD, 1 = HD, 2 = SDOnHD,
    // 24 = Crystal; other values are non-mesh shaders that shouldn't appear on
    // a real layer but we preserve the raw integer so the render path can
    // route pure-HD materials through hd.bls and SD-authored ones through
    // sd_on_hd.bls when render mode = HD.
    int shaderId = 0;

    // Reforged (v1200+) HD subtextures. The HD PS expects distinct
    // textures at t0..t4 for albedo / normal / ORM / emissive / teamcolor.
    // For classic (<v1100) layers only `textureId` (the legacy diffuse
    // slot) is populated and these stay at -1; the HD draw path should
    // then bind the same diffuse to every slot it can and default-fill
    // the rest so the shader doesn't sample garbage.
    int normalMapId    = -1;
    int ormMapId       = -1;
    int emissiveMapId  = -1;
    int teamColorMapId = -1;

    // Per-layer HD material knobs. Feed directly into the HD PS CB
    // pixelParams / fresnelColor slots (see CGxMatParams::PixelParams
    // RE). Defaults keep the fresnel overlay and emissive paths dormant
    // so classic MDX layers render without spurious rim highlights.
    float    emissiveGain    = 0.0f;
    float    fresnelOpacity  = 0.0f;
    float    fresnelTeamColor = 0.0f;
    Vector3f fresnelColor    = {0.0f, 0.0f, 0.0f};
};

struct MaterialData {
    int materialId;
    std::vector<MaterialLayerData> layers;
    int priorityPlane;
    int sortOrder;
};

// Billboard flags for bones. Values mirror MDX Node::NodeFlag semantics:
// only one axis/full flag should be set per node — Previewd's GetObjectFlags
// @0x140456dd0 applies a priority Full > LockX > LockY > LockZ if the file
// set more than one. CAMERA_ANCHORED is an independent bit that can stack.
enum BoneBillboardFlag : uint32_t {
    BONE_BILLBOARD_NONE            = 0,
    BONE_BILLBOARD_FULL            = 1,
    BONE_BILLBOARD_LOCK_X          = 2,
    BONE_BILLBOARD_LOCK_Y          = 4,
    BONE_BILLBOARD_LOCK_Z          = 8,
    BONE_BILLBOARD_CAMERA_ANCHORED = 16,
};

struct SkeletonData {
    int nodeCount;   // palette size: total hierarchy nodes (bones + helpers + emitters etc.)
    std::vector<Matrix44f> inverseBindMatrices;  // nodeCount entries (indexed by node position)
    std::vector<uint32_t> billboardFlags;        // nodeCount entries (indexed by node position)
    std::vector<Vector3f> nodePivots;            // nodeCount entries (indexed by node position)
    std::vector<int>      nodeParents;           // nodeCount entries; -1 = root. Needed for CameraAnchored.
};

// GroupAverageRecord is defined in animation.h alongside VertexInfluence.
//
// PER-GEOSET BONE PALETTE DESIGN (matches Previewd's `usedBonesPerPrimitive`):
// The SD/HD BLS shaders read bone indices as uint8 (ATTR6 = R8G8B8A8_UINT), so
// `boneIdx[k]` must fit in 0..255. Models like nightelf_exp have 650+ bones in
// the hierarchy, well beyond that cap. Instead of a single global palette, each
// geoset carries its own compact subset of the bones it actually references.
//
//   subsetNodeIndices[local] = global hierarchy position
//
// `influences[v].boneIdx[k]` is written as a LOCAL slot index (0..subset.size()
// - 1, plus pseudo slots for groupAverages beyond that).
// `groupAverages[g].pseudoSlot`   is a LOCAL slot index >= subsetNodeIndices.size()
// `groupAverages[g].nodeIndices`  are GLOBAL hierarchy positions — the source
// nodes whose offsetMatrices get averaged each frame into `pseudoSlot`.
// The per-geoset palette CB is rebuilt each frame: local slot i copies
// offsetMatrices_[subsetNodeIndices[i]], group slots get the running average.
struct SkinWeightData {
    int geosetId;
    std::vector<VertexInfluence> influences;
    std::vector<GroupAverageRecord> groupAverages;
    std::vector<int> subsetNodeIndices;
};

struct CollisionShapeData {
    int type;              // Previewd: 0=box, 1=cylinder, 2=sphere, 3=plane
    Vector3f vertices[2]; // min/max for box, center for sphere, endpoints for cylinder
    float radius;
    Vector3f pivot = {0, 0, 0}; // Bind-pose world pivot; vertices[] are offsets from it
};

// Per-frame animated state — computed by the adapter, then passed to ApplyFrameState().
struct FrameState {
    std::vector<Matrix44f>  boneWorldMatrices;  // all hierarchy node world matrices (indexed by node position)
    std::vector<Matrix44f>  geosetTransforms;   // one per geoset (node world TM for unskinned meshes)
    std::vector<float>     geosetAlphas;       // one per geoset
    std::vector<Vector3f>  geosetColors;       // one per geoset

    struct ParticleFrameState {
        int emitterId;
        Matrix44f transform;
        float emissionRate, speed, variation, coneAngle;
        float gravity, width, length, visibility;
    };
    std::vector<ParticleFrameState> particleStates;

    struct RibbonFrameState {
        int emitterId;
        Matrix44f transform;
        float above, below, alpha;
        Vector3f color;
        float visibility;
        int   slot;
    };
    std::vector<RibbonFrameState> ribbonStates;

    std::vector<Matrix44f>  collisionTransforms;

    struct TexAnimState {
        int materialId;
        int layerIndex;
        float uOff, vOff, uTile, vTile;
        float rotation; // Z-axis rotation angle in radians
    };
    std::vector<TexAnimState> texAnims;

    // Evaluated MDX scene light state per frame. Max 8 lights per draw --
    // the SD VS permute radix for numLights is 9 (0..8). The adapter fills
    // {worldPos | worldDir, diffuseRGB, ambientRGB, type} and the renderer
    // transforms position/direction into view space at CB upload time.
    enum class LightKind : uint8_t { Directional = 0, Omni = 1, Ambient = 2 };
    struct LightState {
        LightKind kind       = LightKind::Directional;
        Vector3f  worldPos   = {0, 0, 0};  // Omni: pivot world position
        Vector3f  worldDir   = {0, 0, -1}; // Directional: unit direction away from source
        Vector3f  diffuse    = {0, 0, 0};  // color * intensity, linear
        Vector3f  ambient    = {0, 0, 0};  // ambColor * ambIntensity
        float     attenStart = 0.0f;       // unused by SD shader but kept for parity
        float     attenEnd   = 0.0f;
        bool      enabled    = true;
    };
    std::vector<LightState> lights;

    // Per-textureAnimationId evaluated 2x3 UV affine matrix, shared across
    // every layer that references the same MDX texture animation. Indexed
    // densely by textureAnimationId so the renderer can look it up
    // directly from layer.textureAnimationId. Empty vector = no anims
    // authored; missing entries default to identity at consume time.
    struct TexAnimMatrix {
        int   textureAnimId;
        // 2 rows of 4 floats each; shader reads .xyw only.
        float row0[4];
        float row1[4];
    };
    std::vector<TexAnimMatrix> texAnimMatrices;

    // Per-layer animated alpha (KMTA tracks)
    struct LayerAlphaState {
        int materialId;
        int layerIndex;
        float alpha;
    };
    std::vector<LayerAlphaState> layerAlphas;

    // Per-layer animated texture ID (KMTF tracks)
    struct LayerTextureIdState {
        int materialId;
        int layerIndex;
        int textureId;
    };
    std::vector<LayerTextureIdState> layerTextureIds;

    // Per-layer evaluated HD fresnel/emissive state. Mirrors what
    // Previewd's RenderGeosetLayers (0x14030a210) pulls out of
    // modelptr->m_fresnelColor / m_fresnelOpacity / m_fresnelTeamColor /
    // m_layerEmissive each frame before stamping them into
    // layerMaterial.m_pixelParams. Driven by the MDX layer's
    // fresnelColorTracks / fresnelAlphaTracks / fresnelTeamColorTracks /
    // emissiveGainTracks; a layer without animation tracks falls back to
    // the static authored values at load time and doesn't emit a state.
    struct LayerFresnelState {
        int      materialId;
        int      layerIndex;
        Vector3f fresnelColor;
        float    fresnelOpacity;
        float    fresnelTeamColor;
        float    emissiveGain;
    };
    std::vector<LayerFresnelState> layerFresnels;

    // Attachment per-frame state
    struct AttachmentFrameState {
        int attachmentIndex;
        Matrix44f transform;
        float visibility;
    };
    std::vector<AttachmentFrameState> attachmentStates;

    // PE1 (model particle emitter) per-frame state
    struct PE1FrameState {
        int emitterId;
        Matrix44f transform;
        float emissionRate, speed, latitude, longitude;  // lat/lon in radians
        float gravity, visibility;
    };
    std::vector<PE1FrameState> pe1States;
};

} // namespace WhiteoutDex
