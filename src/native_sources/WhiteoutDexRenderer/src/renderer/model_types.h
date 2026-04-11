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
#include <string>

namespace WhiteoutDex {

// Coordinate space for MDX model data
enum class CoordSpace { MDX, Max };

// Camera preset for the camera selector combo box
struct CameraPreset {
    std::wstring name;
    float pitch, yaw, distance;
    XMFLOAT3 target;
    bool isLive = false;
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

// Billboard flags for bones
enum BoneBillboardFlag : uint32_t {
    BONE_BILLBOARD_NONE    = 0,
    BONE_BILLBOARD_FULL    = 1,
    BONE_BILLBOARD_LOCK_X  = 2,
    BONE_BILLBOARD_LOCK_Y  = 4,
    BONE_BILLBOARD_LOCK_Z  = 8,
};

struct SkeletonData {
    int boneCount;   // total bones that affect skin (indices into inverseBindMatrices)
    int nodeCount;   // total hierarchy nodes (bones + helpers + emitters + collisions)
    std::vector<XMMATRIX> inverseBindMatrices;  // boneCount entries
    std::vector<uint32_t> boneBillboardFlags;    // boneCount entries (BoneBillboardFlag)
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
        int layerIndex;
        float uOff, vOff, uTile, vTile;
        float rotation; // Z-axis rotation angle in radians
    };
    std::vector<TexAnimState> texAnims;

    // Per-layer animated alpha (KMTA tracks)
    struct LayerAlphaState {
        int materialId;
        int layerIndex;
        float alpha;
    };
    std::vector<LayerAlphaState> layerAlphas;

    // PE1 (model particle emitter) per-frame state
    struct PE1FrameState {
        int emitterId;
        XMMATRIX transform;
        float emissionRate, speed, latitude, longitude;  // lat/lon in radians
        float gravity, visibility;
    };
    std::vector<PE1FrameState> pe1States;
};

} // namespace WhiteoutDex
