#pragma once
// ============================================================================
// WhiteoutDex Renderer — ModelTemplate
//
// Cross-instance immutable cache of one parsed MDX file. Promoted out of
// `RenderService::PE1ModelTemplate` (Phase 1) and given a proper home (Phase 2).
//
// Owns:
//   - Static parsed data (meshes, textures, materials, skeleton, configs).
//   - Shared GPU geometry buffers — uploaded once on the first borrowing
//     instance's render-thread pass; every subsequent Actor binds
//     the same handles via Actor::sourceTemplate.
//   - Shared SkinningData (per-vertex weight tables + per-geoset palette
//     layouts + inverse bind matrices). One copy per template, borrowed
//     by every instance through SkinningSystem::SetSharedData.
//   - The parsed MdxModelAdapter — kept alive so PE1 children can drive
//     animation evaluation against the live adapter (a per-actor split
//     comes in Phase 3).
//
// Lifetime: shared_ptr from ModelTemplateManager. Instances also hold
// shared_ptrs (Actor::sourceTemplate). The template's GPU buffers
// must be released before the gfx device is destroyed — see ReleaseGPU.
// ============================================================================

#include "../gfx/gfx.h"
#include "model_types.h"

#include <memory>
#include <string>
#include <vector>

namespace WhiteoutDex {

class MdxModelAdapter;
struct SkinningData;   // defined in animation.h

struct ModelTemplate {
    // Inner type: GPU resource set borrowed by every Actor.
    struct SharedGeoset {
        int               geosetId    = -1;
        gfx::BufferHandle ib          = gfx::BufferHandle::Invalid;
        gfx::BufferHandle unskinnedVb = gfx::BufferHandle::Invalid;
        gfx::BufferHandle tangentVb   = gfx::BufferHandle::Invalid;
        gfx::BufferHandle boneVb      = gfx::BufferHandle::Invalid;
        int               indexCount  = 0;
        int               vertexCount = 0;
        int               materialId  = -1;
        uint32_t          lod         = 0;
    };

    // Parsed adapter — drives animation evaluation for borrowing instances.
    // Will be replaced by an IAnimationSource binding in Phase 3.
    std::shared_ptr<MdxModelAdapter>   adapter;

    // Static parsed data. Will be re-organised into a `ModelData` aggregate
    // during the IModelSource split (Phase 3).
    std::vector<MeshData>              meshes;
    std::vector<TextureData>           textures;
    std::vector<MaterialData>          materials;
    SkeletonData                       skeleton;
    std::vector<SkinWeightData>        skinWeights;
    std::vector<ParticleEmitterConfig> pe2Configs;
    std::vector<RibbonEmitterConfig>   ribbonConfigs;
    std::vector<CollisionShapeData>    collisionConfigs;
    std::vector<PE1EmitterConfig>      pe1Configs;
    std::vector<AttachmentConfig>      attachmentConfigs;
    std::vector<CameraPreset>          cameraPresets;

    // Pre-built shared skinning data. See animation.h::SkinningData.
    std::shared_ptr<SkinningData>      skinningData;

    // Shared GPU geometry. Populated lazily by the render thread in
    // RenderService::uploadTemplateGpu the first time a Actor
    // pointing at this template hits ProcessStagedData.
    bool                               gpuUploaded = false;
    std::vector<SharedGeoset>          sharedGeosets;

    ModelTemplate();
    ~ModelTemplate();
    ModelTemplate(const ModelTemplate&)            = delete;
    ModelTemplate& operator=(const ModelTemplate&) = delete;

    // Free the SharedGeoset GPU handles. Idempotent; called by
    // ModelTemplateManager::ReleaseAllGPU before the gfx device tears down.
    void ReleaseGPU(gfx::IGFXDevice& gfx);
};

} // namespace WhiteoutDex
