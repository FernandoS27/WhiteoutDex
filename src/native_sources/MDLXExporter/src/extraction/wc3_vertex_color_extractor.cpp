// MDLXExporter — Wc3VertexColor modifier extractor implementation
#include "wc3_vertex_color_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>
#include <extraction/modifier_reader.h>

namespace mdx_extract {

void extractVertexColors(const std::vector<core::SceneNode>& nodes,
                         ir::IRModel& model,
                         core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    for (auto& sn : nodes) {
        if (sn.category != core::NodeCategory::Mesh) continue;
        if (!sn.maxNode) continue;

        // Look for the Wc3VertexMod modifier on the node
        auto* mod = core::ModifierReader::findModifier(sn.maxNode, mdx_ids::WC3_VERTEX_MOD);
        if (!mod) continue;

        auto* ref = dynamic_cast<ReferenceTarget*>(mod);
        if (!ref) continue;

        TimeValue t = 0;
        BOOL usesDropShadow = FALSE;
        PBR::readBoolByName(ref, L"UsesDropShadow", t, usesDropShadow);

        // Apply to the corresponding mesh
        for (auto& mesh : model.meshes) {
            if (mesh.nodeIndex == sn.nodeIndex) {
                mesh.hasDropShadow = (usesDropShadow != FALSE);
                break;
            }
        }
    }
}

} // namespace mdx_extract
