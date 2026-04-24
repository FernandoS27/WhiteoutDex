#pragma once
// ============================================================================
// MDX Model Adapter — Translates WhiteoutLib MDX types to IModelSource.
// No Max SDK dependency. Uses WhiteoutLib types.
// ============================================================================

#include "model_source.h"
#include "mdx_animation.h"
#include "file_resolver.h"
#include "renderer/particle/plane_emitter.h"
#include <whiteout/models/mdx/types.h>
#include <string>
#include <filesystem>
#include <vector>

namespace WhiteoutDex {

class IContentProvider;

class MdxModelAdapter : public IModelSource {
public:
    // Construct from a parsed MDX model. basePath is the directory containing
    // the .mdx file, used to resolve relative texture paths.
    // contentProvider: optional; when set, falls back to CASC/MPQ for textures.
    // Coordinate space is controlled at compile time via WDX_DEFAULT_COORD_SPACE
    // (see renderer/coordinate_system.h).
    explicit MdxModelAdapter(whiteout::mdx::Model model,
                             std::filesystem::path basePath = {},
                             IContentProvider* contentProvider = nullptr);

    // ---- IModelSource static data ----
    std::vector<MeshData>              GetMeshes()          override;
    std::vector<TextureData>           GetTextures()        override;
    std::vector<MaterialData>          GetMaterials()       override;
    SkeletonData                       GetSkeleton()        override;
    std::vector<SkinWeightData>        GetSkinWeights()     override;
    std::vector<ParticleEmitterConfig> GetParticleConfigs() override;
    std::vector<RibbonEmitterConfig>   GetRibbonConfigs()   override;

    // PE2 service path (new, co-exists with GetParticleConfigs).
    std::vector<particle::PlaneEmitterInit> GetPlaneEmitterInits() const;
    std::vector<CollisionShapeData>    GetCollisionShapes() override;
    std::vector<AttachmentConfig>      GetAttachmentConfigs() override;
    std::vector<PE1EmitterConfig>      GetPE1Configs()      override;

    // ---- Sequence control ----
    void SetActiveSequence(int sequenceIndex) override;

    // ---- Camera info for billboard nodes ----
    void SetCameraPosition(float x, float y, float z) override;

    // ---- Per-frame evaluation ----
    FrameState Evaluate(int timeMs, int globalTimeMs = -1) override;

    // ---- Sequence info ----
    std::vector<SequenceInfo> GetSequences() override;

    // ---- Camera presets from model ----
    std::vector<CameraPreset> GetCameraPresets() const;

private:
    whiteout::mdx::Model model_;
    std::filesystem::path basePath_;
    FileResolver resolver_;
    IContentProvider* contentProvider_ = nullptr;
    MdxHierarchy hierarchy_;

    // Per-hierarchy-node bone-visibility gate. For each bone node, stores
    // the fs.geosetAlphas index to AND the node (and its subtree) against;
    // -1 means "no gate" (non-bone node, or a bone with geosetId ==
    // MULTIPLE_GEOSETS, or geosetAnimationId unresolved). Cached at load
    // time; mirrors CreateBone @0x1404573d0 which stores
    //   CAnimBoneObj::geosetId = (bone.geosetId == -1) ? -1 : bone.geosetAnimId
    // and CAnimBoneObj::IsVisible's lookup against anim->geosetStatus
    // (indexed by GeosetAnimation index). Previewd's
    // PrepareObjectHierarchyViews skips every descendant of a hidden bone,
    // so we feed this table into a per-frame nodeVisible[] sweep in
    // Evaluate() to zero the visibility of every attachment / emitter /
    // ribbon / light sitting below a hidden bone.
    std::vector<int> boneGateGeoset_;

    // Active sequence
    int activeSeqIdx_ = -1;
    int seqStart_ = 0;
    int seqEnd_   = 0;

    // Camera position for billboard evaluation
    Vector3f cameraPos_ = {0, -350, 50};

    // Helpers
    int MapPE2FilterMode(whiteout::u32 mdxMode) const;
    int MapShadingFlags(whiteout::mdx::Layer::ShadingFlag sf) const;

    TextureData LoadTextureFile(const std::string& path, int textureId,
                                int replaceableId) const;
    TextureData GenerateTeamColorTexture(int textureId, int replaceableId) const;
};

} // namespace WhiteoutDex
