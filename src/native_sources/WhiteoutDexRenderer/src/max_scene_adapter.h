#pragma once
// ============================================================================
// WhiteoutDex Max Scene Adapter — Implements IModelSource for 3ds Max scenes
// Refactored from WhiteoutDexExtractor (extract.h)
// All Max SDK dependencies are isolated here.
// ============================================================================

#include <max.h>
#include <maxversion.h>
#include <iparamb2.h>
#include <modstack.h>
#include <iskin.h>
#include <icustattribcontainer.h>
#include <MeshNormalSpec.h>
#include <bitmap.h>
#include <bmmlib.h>
#include <triobj.h>
#include <stdmat.h>
#include <inode.h>

#include <vector>
#include <unordered_map>
#include <string>
#include <functional>

#include "renderer/model_source.h"
#include "renderer/model_types.h"

// ============================================================================
// Known ClassIDs for WhiteoutDex custom MaxScript plugins
// ============================================================================
#define WARCRAFT3_MAT_CLASS_ID   Class_ID(0x4b8e20a3, 0x1f6c3d57)
#define WC3_BITMAP_CLASS_ID      Class_ID(0x3a7c10f1, 0x5e2d4b08)
#define WC3PARTICLES2_CLASS_ID   Class_ID(0xD9F33BC9, 0x7A0DA37A)
#define WC3RIBBON_CLASS_ID       Class_ID(0x937AA064, 0x9EFFA3DA)
#define WC3VERTEXMOD_CLASS_ID    Class_ID(0x234d68a2, 0x7204a141)
#define WC3PARTICLES1_CLASS_ID   Class_ID(0x12E4F5A6, 0x3B7C8D9E)
#define WC3ATTACHPOINT_CLASS_ID  Class_ID(0x1136ac20, 0x6f9cfeb7)

// Cross-DLL interface IDs
#define WC3P2_TEXTURE_PATH_IID   0x7B3C8D10
#define WC3P2_TEXTURE_PREFIX_IID 0x7B3C8D11
#define WC3P1_MODEL_PATH_IID     0x7B3C8D01

// ============================================================================
// Collected data structures (Max-specific; not exposed to renderer)
// ============================================================================

struct BoneInfo {
    INode* node = nullptr;
    Matrix3 inverseBind;
    int index = 0;
};

struct GeosetInfo {
    int geosetId = 0;
    INode* node = nullptr;
    int materialId = -1;
    std::vector<int> faceVertMap;   // expanded vertex → original vertex
    int expandedVertCount = 0;
};

struct MaterialLayerInfo {
    int filterMode = 0;
    int textureId = -1;
    float alpha = 1.0f;
    int replaceableTexture = 0;
    int flags = 0;
};

struct MaterialInfo {
    int materialId = 0;
    Mtl* mtl = nullptr;
    std::vector<MaterialLayerInfo> layers;  // one per layer (composite sub-materials)
    int priorityPlane = 0;
    int sortOrder = 0;
};

struct TextureEntry {
    int textureId = 0;
    int replaceableId = 0;
    std::wstring filePath;
};

struct ParticleEmitterInfo {
    int emitterId = 0;
    INode* node = nullptr;
    int textureId = -1;
};

struct PE1EmitterInfo {
    int emitterId = 0;
    INode* node = nullptr;
    std::string modelPath;
};

struct AttachmentInfo {
    int index = 0;
    INode* node = nullptr;
    int attachmentId = 0;
    std::string modelPath;
};

struct RibbonEmitterInfo {
    int emitterId = 0;
    INode* node = nullptr;
    int textureId = -1;
};

struct CollisionShapeInfo {
    int type = 0;
    INode* node = nullptr;
};

// ============================================================================
// MaxSceneAdapter — implements IModelSource
// ============================================================================
namespace WhiteoutDex {

class MaxSceneAdapter : public IModelSource {
public:
    MaxSceneAdapter();
    ~MaxSceneAdapter() override;

    // Collect scene data (call once on main thread before Get*() calls)
    void CollectScene();

    // Re-read material properties and textures from the scene.
    // Returns true if anything changed and the renderer should be updated.
    struct MaterialRefreshResult {
        std::vector<MaterialData> materials;
        std::vector<TextureData>  textures;
        bool changed = false;
    };
    MaterialRefreshResult RefreshMaterials();

    // IModelSource interface
    std::vector<MeshData>              GetMeshes()          override;
    std::vector<TextureData>           GetTextures()        override;
    std::vector<MaterialData>          GetMaterials()       override;
    SkeletonData                       GetSkeleton()        override;
    std::vector<SkinWeightData>        GetSkinWeights()     override;
    std::vector<ParticleEmitterConfig> GetParticleConfigs() override;
    std::vector<RibbonEmitterConfig>   GetRibbonConfigs()   override;
    std::vector<CollisionShapeData>    GetCollisionShapes() override;
    std::vector<AttachmentConfig>      GetAttachmentConfigs() override;
    std::vector<PE1EmitterConfig>      GetPE1Configs()      override;

    void SetActiveSequence(int sequenceIndex) override;  // no-op for Max

    FrameState Evaluate(int timeMs) override;

    std::vector<SequenceInfo> GetSequences() override;

    // Camera presets from scene (Max cameras + "Active Viewport")
    std::vector<WhiteoutDex::CameraPreset> GetCameraPresets();

private:
    // Collection phases
    void CollectGeometry();
    void CollectMaterials();
    int  LoadTexture(const std::wstring& filePath, int replaceableId);
    int  LoadTextureWithTeamColor(const std::wstring& filePath, int tcR, int tcG, int tcB);
    int  GenerateTeamGlowTexture(int tcR, int tcG, int tcB);
    void CollectBones();
    void CollectAttachments();
    void CollectParticleEmitters();
    void CollectRibbonEmitters();
    void CollectCollisionShapes();

    // Helpers
    void PackMatrix(const Matrix3& tm, float* dst);
    static bool PB2Float(Animatable* a, const wchar_t* name, TimeValue t, float& out);
    static bool PB2Int(Animatable* a, const wchar_t* name, TimeValue t, int& out);
    static bool PB2Bool(Animatable* a, const wchar_t* name, TimeValue t, BOOL& out);
    static bool PB2Color(Animatable* a, const wchar_t* name, TimeValue t, Color& out);
    static bool PB2Texmap(Animatable* a, const wchar_t* name, Texmap*& out);
    static Object* GetBaseObject(INode* node);
    static Modifier* FindSkinModifier(INode* node);
    static Modifier* FindModifierByClassID(INode* node, Class_ID cid);
    MaterialLayerInfo ExtractWc3MaterialLayer(Mtl* mtl);
    static int MapMaterialFilterMode(int wc3fm);
    static int MapParticleFilterMode(int bpfm);
    static int MapRibbonFilterMode(int rbfm);
    std::wstring GetMaxFilePath();

    // Loaded texture pixel data (kept for GetTextures())
    struct LoadedTexture {
        int textureId;
        int replaceableId;
        std::vector<uint8_t> rgba;
        int width, height;
    };
    std::vector<LoadedTexture> loadedTextures_;

    std::vector<BoneInfo>            bones_;
    std::unordered_map<std::wstring, int> boneNameToIdx_;
    std::vector<GeosetInfo>          geosets_;
    std::vector<MaterialInfo>        materials_;
    std::vector<TextureEntry>        texEntries_;
    std::vector<ParticleEmitterInfo> particles_;
    std::vector<PE1EmitterInfo>      pe1Emitters_;
    std::vector<AttachmentInfo>      attachments_;
    std::vector<RibbonEmitterInfo>   ribbons_;
    std::vector<CollisionShapeInfo>  collisions_;

    std::unordered_map<std::wstring, int> texPathToId_;
    std::unordered_map<Mtl*, int>         mtlToId_;
    int nextTexId_ = 0;
    int nextMatId_ = 0;

    // Material change detection: snapshot of per-material properties
    struct MaterialSnapshot {
        int filterMode = 0;
        int flags = 0;
        int priorityPlane = 0;
        int sortOrder = 0;
        int replaceableTexture = 0;
        std::wstring texturePath;
    };
    std::unordered_map<int, MaterialSnapshot> matSnapshots_;  // materialId → snapshot
};

} // namespace WhiteoutDex
