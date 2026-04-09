#pragma once
// ============================================================================
// WhiteoutDex Renderer — Common Model Types
// Strongly-typed intermediate structs for the IModelSource adapter pattern.
// Neither Max SDK nor WhiteoutLib types appear here.
// ============================================================================

#include "types.h"
#include "particle.h"
#include "ribbon.h"
#include "animation.h"
#include <vector>

namespace WhiteoutDex {

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
    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> uvs;
    std::vector<uint32_t>  indices;
};

struct TextureData {
    int textureId;
    int replaceableId;
    std::vector<uint8_t> rgba;  // RGBA8 pixels
    int width, height;
};

struct MaterialLayerData {
    int filterMode;
    int textureId;
    float alpha;
    int flags;  // MAT_TWO_SIDED, MAT_UNSHADED, etc. (from FilterMode/MaterialFlags enums)
};

struct MaterialData {
    int materialId;
    std::vector<MaterialLayerData> layers;
    int priorityPlane;
    int sortOrder;
};

struct SkeletonData {
    int boneCount;   // total bones that affect skin (indices into inverseBindMatrices)
    int nodeCount;   // total hierarchy nodes (bones + helpers + emitters + collisions)
    std::vector<XMMATRIX> inverseBindMatrices;  // boneCount entries
    // nodeCount is needed internally by the animation evaluator to traverse
    // the full hierarchy (emitters, collisions, etc.). FrameState::boneWorldMatrices
    // carries boneCount entries (skinning bones only); emitter/collision transforms
    // are delivered via their own FrameState fields.
};

struct SkinWeightData {
    int geosetId;
    std::vector<VertexInfluence> influences;
};

struct CollisionShapeData {
    int type;              // 0=box, 1=sphere, 2=plane, 3=cylinder
    XMFLOAT3 vertices[2]; // min/max for box, center for sphere
    float radius;
};

// Per-frame animated state — computed by the adapter, then passed to ApplyFrameState().
struct FrameState {
    std::vector<XMMATRIX>  boneWorldMatrices;  // boneCount entries (skinning bones)
    std::vector<XMMATRIX>  geosetTransforms;   // one per geoset (node world TM for unskinned meshes)
    std::vector<float>     geosetAlphas;       // one per geoset
    std::vector<XMFLOAT3>  geosetColors;       // one per geoset

    struct ParticleFrameState {
        int emitterId;
        XMMATRIX transform;
        float emissionRate, speed, variation, coneAngle;
        float gravity, width, length, visibility;
    };
    std::vector<ParticleFrameState> particleStates;

    struct RibbonFrameState {
        int emitterId;
        XMMATRIX transform;
        float above, below, alpha;
        XMFLOAT3 color;
        float visibility;
        int   slot;
    };
    std::vector<RibbonFrameState> ribbonStates;

    std::vector<XMMATRIX>  collisionTransforms;

    struct TexAnimState {
        int materialId;
        float uOff, vOff, uTile, vTile;
    };
    std::vector<TexAnimState> texAnims;
};

} // namespace WhiteoutDex
