// MDLXExporter — Wc3Ribbon extractor implementation
#include "wc3_ribbon_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>

// ParamIDs from Wc3Ribbon/Ribbon.h
namespace {
    enum RibbonParams : ParamID {
        pb_height_above = 0,
        pb_height_below = 1,
        pb_edges_per_second = 2,
        pb_edge_lifetime = 3,
        pb_tex_rows = 4,
        pb_tex_cols = 5,
        pb_tex_slot = 6,
        pb_material = 7,
        pb_color = 8,
        pb_alpha = 9,
        pb_gravity = 10,
    };
}

namespace mdx_extract {

void extractRibbons(const std::vector<core::SceneNode>& nodes,
                    ir::IRModel& model,
                    const MaterialMap& mtlToIndex,
                    core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3Ribbon") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        IParamBlock2* pb = PBR::findParamBlock(ref, 0);
        if (!pb) continue;

        TimeValue t = 0;
        ir::RibbonEmitter rib;
        rib.nodeIndex = sn.nodeIndex;

        rib.heightAbove = PBR::readFloat(pb, pb_height_above, t);
        rib.heightBelow = PBR::readFloat(pb, pb_height_below, t);
        rib.lifespan = PBR::readFloat(pb, pb_edge_lifetime, t);
        rib.gravity = PBR::readFloat(pb, pb_gravity, t);
        rib.emissionRate = PBR::readInt(pb, pb_edges_per_second, t);
        rib.rows = PBR::readInt(pb, pb_tex_rows, t, 1);
        rib.columns = PBR::readInt(pb, pb_tex_cols, t, 1);
        rib.textureSlot = PBR::readInt(pb, pb_tex_slot, t);

        rib.alpha = PBR::readFloat(pb, pb_alpha, t, 1.0f);

        Point3 col = PBR::readPoint3(pb, pb_color, t, Point3(1, 1, 1));
        rib.color = Color(col.x, col.y, col.z);

        // Material reference: pb_material points to a Max material
        Mtl* mtl = pb->GetMtl(pb_material, t);
        if (mtl) {
            auto it = mtlToIndex.find(mtl);
            if (it != mtlToIndex.end())
                rib.materialIndex = it->second;
        }

        model.ribbonEmitters.push_back(std::move(rib));
    }
}

} // namespace mdx_extract
