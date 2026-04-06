// MDLXImporter — MDX Model Disassembler: mdx::Model → ir::IRModel
#pragma once

#include <core/intermediate_types.h>
#include <whiteout/models/mdx/types.h>
#include "mdx_hierarchy_mapper.h"
#include "../mdlx_import_options.h"

namespace mdx_disasm {

/// Converts a parsed whiteout::mdx::Model into the format-agnostic ir::IRModel.
/// Applies coordinate transforms and time conversion during mapping.
class MdxModelDisassembler {
public:
    ir::IRModel disassemble(const whiteout::mdx::Model& mdxModel,
                            const MdlxImportOptions& options);

private:
    void mapNodes(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapBones(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapSequences(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapGlobalSequences(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapTextures(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapMaterials(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapTextureAnimations(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapGeosets(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapGeosetAnimations(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapLights(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapAttachments(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapParticleEmitters(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapParticleEmitters2(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapRibbonEmitters(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapEventObjects(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapCameras(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapCollisionShapes(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapCornEmitters(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapFaceEffects(const whiteout::mdx::Model& mdx, ir::IRModel& ir);
    void mapNodeAnimations(const whiteout::mdx::Model& mdx, ir::IRModel& ir);

    /// Store a float track in the IR's shared pool, return its index.
    int32_t storeFloatTrack(ir::IRModel& ir, ir::FloatTrack&& track);
    int32_t storeColorTrack(ir::IRModel& ir, ir::ColorTrack&& track);
    int32_t storeVec3Track(ir::IRModel& ir, ir::Vec3Track&& track);
    int32_t storeVec4Track(ir::IRModel& ir, ir::Vec4Track&& track);
    int32_t storeIntTrack(ir::IRModel& ir, ir::IntTrack&& track);

    MdxHierarchyMapper hierarchy_;
    uint32_t version_ = 800;
};

} // namespace mdx_disasm
