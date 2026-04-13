#pragma once
// ============================================================================
// MDX Model Adapter — Translates WhiteoutLib MDX types to IModelSource.
// No Max SDK dependency. Uses WhiteoutLib + DirectXMath.
// ============================================================================

#include "model_source.h"
#include "mdx_animation.h"
#include "file_resolver.h"
#include <whiteout/models/mdx/types.h>
#include <string>
#include <filesystem>

namespace WhiteoutDex {

class FileContentProvider;

class MdxModelAdapter : public IModelSource {
public:
    // Construct from a parsed MDX model. basePath is the directory containing
    // the .mdx file, used to resolve relative texture paths.
    // space: MDX = raw coordinates, Max = swizzle (y, -x, z) for Max integration
    // contentProvider: optional; when set, falls back to CASC/MPQ for textures
    explicit MdxModelAdapter(whiteout::mdx::Model model,
                             std::filesystem::path basePath = {},
                             CoordSpace space = CoordSpace::MDX,
                             FileContentProvider* contentProvider = nullptr);

    // ---- IModelSource static data ----
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
    FileContentProvider* contentProvider_ = nullptr;
    MdxHierarchy hierarchy_;

    // Active sequence
    int activeSeqIdx_ = -1;
    int seqStart_ = 0;
    int seqEnd_   = 0;

    // Camera position for billboard evaluation
    XMFLOAT3 cameraPos_ = {0, -350, 50};

    // Helpers
    int MapPE2FilterMode(whiteout::u32 mdxMode) const;
    int MapLayerFilterMode(whiteout::mdx::Layer::FilterMode fm) const;
    int MapShadingFlags(whiteout::mdx::Layer::ShadingFlag sf) const;

    TextureData LoadTextureFile(const std::string& path, int textureId,
                                int replaceableId) const;
    TextureData GenerateTeamColorTexture(int textureId, int replaceableId) const;
};

} // namespace WhiteoutDex
