// MDLXExporter — Wc3Ribbon extractor implementation
// CHANGES: NeoDex BlizRibbon compatibility via ClassID-based branching.
//   - WhiteoutDex: ORIGINAL index-based (unchanged)
//   - NeoDex:      name-based with BlizRibbon.ms param names

#include "wc3_ribbon_extractor.h"
#include "../mdx_class_ids.h"
#include "controller_track_helper.h"
#include "visibility_track_helper.h"
#include <animation/global_sequence_helper.h>
#include <scene/paramblock_reader.h>
#include <modstack.h>

namespace {

// WhiteoutDex ParamIDs (from Ribbon.h)
enum RibbonParams : ParamID {
    pb_height_above = 0,  pb_height_below = 1,
    pb_edges_per_second = 2,  pb_edge_lifetime = 3,
    pb_tex_rows = 4,  pb_tex_cols = 5,
    pb_tex_slot = 6,  pb_material = 7,
    pb_color = 8,  pb_alpha = 9,  pb_gravity = 10,
};

// Find material param by name across all paramblocks
Mtl* findMtlByName(ReferenceTarget* ref, const wchar_t* name, TimeValue t) {
    int numPB = ref->NumParamBlocks();
    for (int i = 0; i < numPB; ++i) {
        IParamBlock2* pb = ref->GetParamBlock(i);
        if (!pb) continue;
        int n = pb->NumParams();
        for (int idx = 0; idx < n; ++idx) {
            ParamID pid = pb->IndextoID(idx);
            const ParamDef& def = pb->GetParamDef(pid);
            if (!def.int_name || def.type != TYPE_MTL) continue;
            if (_wcsicmp(def.int_name, name) == 0)
                return pb->GetMtl(pid, t);
        }
    }
    return nullptr;
}

} // anon

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

        // Detect plugin type
        Object* baseObj = obj;
        while (baseObj && baseObj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
            baseObj = static_cast<IDerivedObject*>(baseObj)->GetObjRef();
        Class_ID cid = baseObj ? baseObj->ClassID() : Class_ID(0,0);
        const bool isNeoDex = (cid == mdx_ids::NEODEX_RIBBON);

        TimeValue t = 0;
        ir::RibbonEmitter rib;
        rib.nodeIndex = sn.nodeIndex;

        if (!isNeoDex) {
            // ═══ WHITEOUTDEX — original index-based, UNCHANGED ═══
            IParamBlock2* pb = PBR::findParamBlock(ref, 0);
            if (!pb) continue;

            rib.heightAbove = PBR::readFloat(pb, pb_height_above, t);
            rib.heightBelow = PBR::readFloat(pb, pb_height_below, t);
            rib.lifespan    = PBR::readFloat(pb, pb_edge_lifetime, t);
            rib.gravity     = PBR::readFloat(pb, pb_gravity, t);
            rib.emissionRate = PBR::readInt(pb, pb_edges_per_second, t);
            rib.rows         = PBR::readInt(pb, pb_tex_rows, t, 1);
            rib.columns      = PBR::readInt(pb, pb_tex_cols, t, 1);
            rib.textureSlot  = PBR::readInt(pb, pb_tex_slot, t);
            rib.alpha        = PBR::readFloat(pb, pb_alpha, t, 1.0f);

            Point3 col = PBR::readPoint3(pb, pb_color, t, Point3(1,1,1));
            rib.color = Color(col.x, col.y, col.z);

            // Material
            Mtl* mtl = pb->GetMtl(pb_material, t);
            if (mtl) {
                auto it = mtlToIndex.find(mtl);
                if (it != mtlToIndex.end()) rib.materialIndex = it->second;
            }

            // KRHA / KRHB / KRAL / KRCO / KRTX
            rib.heightAboveTrackIndex = extractFloatControllerTrack(
                pb->GetControllerByID(pb_height_above, 0), model);
            rib.heightBelowTrackIndex = extractFloatControllerTrack(
                pb->GetControllerByID(pb_height_below, 0), model);
            rib.alphaTrackIndex = extractFloatControllerTrack(
                pb->GetControllerByID(pb_alpha, 0), model);
            rib.colorTrackIndex = extractColorControllerTrack(
                pb->GetControllerByID(pb_color, 0), model);
            rib.textureSlotTrackIndex = extractIntControllerTrack(
                pb->GetControllerByID(pb_tex_slot, 0), model);

        } else {
            // ═══ NEODEX — name-based with BlizRibbon.ms names ═══
            PBR::readFloatByName(ref, L"Above",   t, rib.heightAbove);
            PBR::readFloatByName(ref, L"Below",   t, rib.heightBelow);
            PBR::readFloatByName(ref, L"life",    t, rib.lifespan);
            PBR::readFloatByName(ref, L"gravity", t, rib.gravity);
            { float e = 10; PBR::readFloatByName(ref, L"emission", t, e);
              rib.emissionRate = e; }
            { int v = 1; PBR::readIntByName(ref, L"rows",    t, v); rib.rows = v; }
            { int v = 1; PBR::readIntByName(ref, L"columns", t, v); rib.columns = v; }
            { int v = 0; PBR::readIntByName(ref, L"slots",   t, v); rib.textureSlot = v; }
            PBR::readFloatByName(ref, L"alpha", t, rib.alpha);

            Color col(1,1,1);
            PBR::readColorByName(ref, L"vertexcolor", t, col);
            rib.color = col;

            // Material by name
            Mtl* mtl = findMtlByName(ref, L"rmaterial", t);
            if (mtl) {
                auto it = mtlToIndex.find(mtl);
                if (it != mtlToIndex.end()) rib.materialIndex = it->second;
            }

            using core::anim::getParamControllerDirect;
            rib.heightAboveTrackIndex = extractFloatControllerTrack(
                getParamControllerDirect(ref, L"Above"), model);
            rib.heightBelowTrackIndex = extractFloatControllerTrack(
                getParamControllerDirect(ref, L"Below"), model);
            rib.alphaTrackIndex = extractFloatControllerTrack(
                getParamControllerDirect(ref, L"alpha"), model);
            rib.colorTrackIndex = extractColorControllerTrack(
                getParamControllerDirect(ref, L"vertexcolor"), model);
            rib.textureSlotTrackIndex = extractIntControllerTrack(
                getParamControllerDirect(ref, L"slots"), model);
        }

        // Common: visibility
        rib.visibilityTrackIndex = extractVisibilityTrack(sn.maxNode, model);

        model.ribbonEmitters.push_back(std::move(rib));
    }
}

} // namespace mdx_extract
