// MaxCore — Intermediate Representation Types
// Format-agnostic data structures for the export pipeline.
// All coordinates are in Max space; all times are in Max ticks (4800/sec).

#pragma once

#include <max.h>
#include <inode.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ir {

// ── Geometry ────────────────────────────────────────────────

struct SkinInfluence {
    int32_t boneIndex = -1;
    float weight = 0.0f;
};

struct Vertex {
    Point3 position;
    Point3 normal;
    std::array<Point2, 4> uvSets = {};
    int32_t uvSetCount = 0;
    Point4 tangent;
    bool hasTangent = false;
    std::vector<SkinInfluence> skinInfluences;
    Color vertexColor;
    bool hasVertexColor = false;
};

struct Mesh {
    std::string name;
    int32_t nodeIndex = -1;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    int32_t materialIndex = -1;
    bool hasDropShadow = false;

    // LOD (v1000+ only): level-of-detail index. 0 = highest detail / main mesh,
    // 1,2,... = lower detail HD layers. Populated from the `Wc3GeosetLod`
    // UserProp on import; default 0 for new meshes and all v800 meshes.
    int32_t lod = 0;

    // LOD name (v1000+ only): semantic label like "head", "body", "cape".
    // Populated from the `Wc3LodName` UserProp when present. Empty string
    // for v800 geosets or v1000+ geosets without a lodName.
    std::string lodName;

    struct SequenceExtent {
        Point3 minBound, maxBound;
        float boundRadius = 0.0f;
    };
    std::vector<SequenceExtent> sequenceExtents;
    SequenceExtent globalExtent;
};

// ── Skeleton ────────────────────────────────────────────────

enum class BoneType {
    Standard,
    Biped,
    CAT,
    IKAffected,
    Helper,
};

struct Bone {
    std::string name;
    int32_t nodeIndex = -1;
    int32_t parentIndex = -1;
    BoneType type = BoneType::Standard;
    Point3 pivotPoint;
    Matrix3 bindPose;
    bool isHelper = false;
    uint32_t nodeFlags = 0;  // MDX node flags (DontInherit, Billboard, etc.)

    // MDX Bone chunk visibility gating fields (see whiteout::mdx::Bone in
    // structures.h: geosetId / geosetAnimationId).
    //
    // -1 encodes MDX MULTIPLE_GEOSETS (0xFFFFFFFF):
    //   * geosetIndex == -1           → bone is always visible (never gated).
    //   * geosetAnimationIndex == -1  → bone has no visibility gate authored.
    //
    // Rule (per Previewd engine): when the bone IS gated, its visibility
    // follows the KGAO alpha track of
    //   IRModel::geosetAnims[geosetAnimationIndex]
    // NOT the GeosetAnim whose meshIndex happens to match geosetIndex.
    // Additionally, hidden bones hide their entire subtree (ancestor
    // inheritance) — non-bone descendants must multiply their own
    // visibility by the product of ancestor bone gates.
    int32_t geosetIndex = -1;
    int32_t geosetAnimationIndex = -1;
};

// ── Materials & Textures ────────────────────────────────────

struct Texture {
    std::string filePath;
    int32_t replaceableId = 0;
    bool wrapU = true;
    bool wrapV = true;

    // Export-only: full on-disk path of the source bitmap. Populated by the
    // exporter when the texture comes from a Max BitmapTex; left empty when
    // the texture has no file backing (e.g. replaceable-id-only entries).
    // Used to drive optional texture conversion at export time.
    std::string sourceDiskPath;
};

enum class BlendMode : uint32_t {
    None = 0,
    Transparent = 1,
    Blend = 2,
    Additive = 3,
    AddAlpha = 4,
    Modulate = 5,
    Modulate2x = 6,
};

enum class TextureSlot {
    Diffuse, Normal, ORM, Emissive, Reflection, TeamColor, Environment
};

struct TextureRef {
    int32_t textureIndex = -1;
    TextureSlot slot = TextureSlot::Diffuse;
};

struct MaterialLayer {
    BlendMode blendMode = BlendMode::None;
    std::vector<TextureRef> textureRefs;
    float alpha = 1.0f;
    int32_t uvSetIndex = 0;

    bool unshaded = false;
    bool twoSided = false;
    bool noDepthTest = false;
    bool noDepthWrite = false;
    bool sphereEnvMap = false;
    bool unfogged = false;

    int32_t alphaTrackIndex = -1;
    int32_t textureIdTrackIndex = -1;
    int32_t textureAnimationIndex = -1;

    float emissiveGain = 0.0f;
    float fresnelOpacity = 0.0f;
    float fresnelTeamColor = 0.0f;
    Point3 fresnelColor = Point3(1.0f, 1.0f, 1.0f);

    int32_t emissiveGainTrackIndex = -1;
    int32_t fresnelColorTrackIndex = -1;
    int32_t fresnelAlphaTrackIndex = -1;
    int32_t fresnelTeamColorTrackIndex = -1;
};

struct Material {
    std::string name;
    std::string shaderName;
    int32_t priorityPlane = 0;
    uint32_t flags = 0;
    std::vector<MaterialLayer> layers;
};

struct TextureAnimation {
    int32_t translationTrackIndex = -1;
    int32_t rotationTrackIndex = -1;
    int32_t scaleTrackIndex = -1;
    int32_t globalSequenceIndex = -1;
};

// ── Animation ────────────────────────────────────────────────

enum class InterpolationType { None, Linear, Hermite, Bezier };

template <typename T>
struct Keyframe {
    TimeValue time = 0;
    T value{};
    T inTangent{};
    T outTangent{};
    bool hasTangents = false;
};

template <typename T>
struct Track {
    InterpolationType interpolation = InterpolationType::None;
    int32_t globalSequenceIndex = -1;
    std::vector<Keyframe<T>> keys;

    bool empty() const { return keys.empty(); }
};

using Vec3Track = Track<Point3>;
using QuatTrack = Track<Quat>;
using FloatTrack = Track<float>;
using ColorTrack = Track<Color>;
using IntTrack = Track<int32_t>;
using Vec4Track = Track<Point4>;

struct NodeAnimation {
    int32_t nodeIndex = -1;
    Vec3Track translation;
    QuatTrack rotation;
    Vec3Track scale;
};

struct Sequence {
    std::string name;
    TimeValue startTime = 0;
    TimeValue endTime = 0;
    bool isLooping = true;
    float rarity = 0.0f;
    float moveSpeed = 0.0f;
    uint32_t flags = 0;
    Point3 extentMin, extentMax;
    float extentRadius = 0.0f;
};

// ── Scene Objects ────────────────────────────────────────────

struct Light {
    int32_t nodeIndex = -1;
    enum class Type { Omni, Directional, Ambient } type = Type::Omni;
    float attenuationStart = 0.0f;
    float attenuationEnd = 100.0f;
    Color color = Color(1.0f, 1.0f, 1.0f);
    float intensity = 1.0f;
    Color ambientColor = Color(0.0f, 0.0f, 0.0f);
    float ambientIntensity = 0.0f;

    int32_t attStartTrackIndex = -1;
    int32_t attEndTrackIndex = -1;
    int32_t colorTrackIndex = -1;
    int32_t intensityTrackIndex = -1;
    int32_t ambColorTrackIndex = -1;
    int32_t ambIntensityTrackIndex = -1;
    int32_t visibilityTrackIndex = -1;
    int32_t shadowIntensityTrackIndex = -1;
};

struct Attachment {
    int32_t nodeIndex = -1;
    std::string name;
    std::string path;
    int32_t attachmentId = 0;
    int32_t visibilityTrackIndex = -1;
};

struct ParticleEmitter {
    int32_t nodeIndex = -1;
    int32_t variant = 2;

    float emissionRate = 0.0f;
    float speed = 0.0f;
    float variation = 0.0f;
    float lifespan = 0.0f;
    float gravity = 0.0f;
    float latitude = 0.0f;
    float longitude = 0.0f;   // PE1 only — PE2 derives internally from LineEmitter
    float width = 0.0f;
    float length = 0.0f;
    float tailLength = 0.0f;

    int32_t filterMode = 0;
    int32_t rows = 1;
    int32_t columns = 1;
    int32_t headOrTail = 0;
    int32_t priorityPlane = 0;
    std::array<Color, 3> segmentColors = {};
    std::array<float, 3> segmentAlpha = {};
    std::array<float, 3> segmentScale = {};
    std::array<int32_t, 3> headInterval = {};
    std::array<int32_t, 3> headDecayInterval = {};
    std::array<int32_t, 3> tailInterval = {};
    std::array<int32_t, 3> tailDecayInterval = {};
    float midTime = 0.5f;

    std::string modelPath;

    // CornEmitter (variant == 3) only: sequence-name visibility gate the
    // PopcornFX runtime evaluates against the currently playing animation
    // (e.g. "Stand=on,Death=off"). Empty for PE1 / PE2. Round-tripped
    // verbatim — the renderer parses it, the exporter just writes it back.
    std::string animVisibilityGuide;

    int32_t textureIndex = -1;
    int32_t replaceableId = 0;
    uint32_t flags = 0;

    int32_t emissionRateTrackIndex = -1;
    int32_t speedTrackIndex = -1;
    int32_t variationTrackIndex = -1;
    int32_t lifespanTrackIndex = -1;
    int32_t gravityTrackIndex = -1;
    int32_t visibilityTrackIndex = -1;
    int32_t latitudeTrackIndex = -1;
    int32_t longitudeTrackIndex = -1;  // PE1 only
    int32_t widthTrackIndex = -1;
    int32_t lengthTrackIndex = -1;
    int32_t lifespanVariationTrackIndex = -1;
    int32_t colorTrackIndex = -1;
};

struct RibbonEmitter {
    int32_t nodeIndex = -1;
    float heightAbove = 0.0f;
    float heightBelow = 0.0f;
    float alpha = 1.0f;
    float lifespan = 0.0f;
    float gravity = 0.0f;
    Color color = Color(1.0f, 1.0f, 1.0f);
    int32_t materialIndex = -1;
    int32_t textureSlot = 0;
    int32_t emissionRate = 0;
    int32_t rows = 1;
    int32_t columns = 1;

    int32_t heightAboveTrackIndex = -1;
    int32_t heightBelowTrackIndex = -1;
    int32_t alphaTrackIndex = -1;
    int32_t colorTrackIndex = -1;
    int32_t visibilityTrackIndex = -1;
    int32_t textureSlotTrackIndex = -1;
};

struct EventObject {
    int32_t nodeIndex = -1;
    std::string eventCode;
    std::string eventData;
    std::vector<TimeValue> keyTimes;
};

struct CollisionShape {
    int32_t nodeIndex = -1;
    enum class Shape { Box, Sphere, Cylinder, Plane } shape = Shape::Box;
    std::vector<Point3> vertices;
    float radius = 0.0f;
};

struct Camera {
    std::string name;
    Point3 position;
    Point3 targetPosition;
    float fov = 0.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    int32_t positionTrackIndex = -1;
    int32_t targetPositionTrackIndex = -1;
    int32_t rotationTrackIndex = -1;
};

// ── Format-Specific Extension Hooks ──────────────────────────

struct ExtensionData {
    virtual ~ExtensionData() = default;
};

// ── Root Model Container ─────────────────────────────────────

struct IRModel {
    struct Node {
        std::string name;
        INode* maxNode = nullptr;
        int32_t parentIndex = -1;
        Point3 pivotPoint;
        Matrix3 worldTM;
        uint32_t nodeFlags = 0;
        std::unique_ptr<ExtensionData> ext;
    };
    std::vector<Node> nodes;

    std::vector<Mesh> meshes;
    std::vector<Bone> bones;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<TextureAnimation> textureAnimations;

    std::vector<Sequence> sequences;
    std::vector<uint32_t> globalSequenceDurations;
    std::vector<NodeAnimation> nodeAnimations;

    std::vector<FloatTrack> floatTracks;
    std::vector<Vec3Track> vec3Tracks;
    std::vector<QuatTrack> quatTracks;
    std::vector<ColorTrack> colorTracks;
    std::vector<IntTrack> intTracks;
    std::vector<Vec4Track> vec4Tracks;

    std::vector<Light> lights;
    std::vector<Attachment> attachments;
    std::vector<ParticleEmitter> particleEmitters;
    std::vector<RibbonEmitter> ribbonEmitters;
    std::vector<EventObject> eventObjects;
    std::vector<CollisionShape> collisionShapes;
    std::vector<Camera> cameras;

    struct GeosetAnim {
        int32_t meshIndex = -1;
        float alpha = 1.0f;
        Color color = Color(1.0f, 1.0f, 1.0f);
        bool usesColor = false;
        bool dropShadow = false;
        int32_t alphaTrackIndex = -1;
        int32_t colorTrackIndex = -1;
    };
    std::vector<GeosetAnim> geosetAnims;
};

} // namespace ir
