// MDLXImporter — MdxHierarchyMapper implementation
#include "mdx_hierarchy_mapper.h"

namespace wdx = whiteout::mdx;

namespace mdx_disasm {

void MdxHierarchyMapper::build(const wdx::Model& mdx) {
    nodes_.clear();
    objectIdToIrIndex_.clear();

    // Helper lambda to register a node
    auto registerNode = [&](uint32_t objectId, uint32_t parentId, bool isHelper) {
        MappedNode mn;
        mn.irIndex = static_cast<int32_t>(nodes_.size());
        mn.mdxObjectId = objectId;
        mn.isHelper = isHelper;

        // Resolve parent: 0xFFFFFFFF means root
        if (parentId != 0xFFFFFFFF) {
            auto it = objectIdToIrIndex_.find(parentId);
            mn.irParent = (it != objectIdToIrIndex_.end()) ? it->second : -1;
        }

        objectIdToIrIndex_[objectId] = mn.irIndex;
        nodes_.push_back(mn);
    };

    // ── Bones ──
    for (const auto& bone : mdx.bones) {
        // v1200: helpers packed into bones are identified by NodeType::Helper
        bool helper = (bone.node.type == wdx::Node::NodeType::Helper);
        registerNode(bone.node.objectId, bone.node.parentId, helper);
    }

    // ── Helpers (v800 — separate array; v1200 has empty helpers array) ──
    for (const auto& helper : mdx.helpers) {
        registerNode(helper.node.objectId, helper.node.parentId, true);
    }

    // ── Lights ──
    for (const auto& light : mdx.lights) {
        registerNode(light.node.objectId, light.node.parentId, false);
    }

    // ── Attachments ──
    for (const auto& att : mdx.attachments) {
        registerNode(att.node.objectId, att.node.parentId, false);
    }

    // ── Particle Emitters 1 ──
    for (const auto& pe : mdx.particleEmitters) {
        registerNode(pe.node.objectId, pe.node.parentId, false);
    }

    // ── Particle Emitters 2 ──
    for (const auto& pe2 : mdx.particleEmitters2) {
        registerNode(pe2.node.objectId, pe2.node.parentId, false);
    }

    // ── Ribbon Emitters ──
    for (const auto& rib : mdx.ribbonEmitters) {
        registerNode(rib.node.objectId, rib.node.parentId, false);
    }

    // ── Event Objects ──
    for (const auto& evt : mdx.eventObjects) {
        registerNode(evt.node.objectId, evt.node.parentId, false);
    }

    // ── Cameras have no ObjectID in the MDX node hierarchy ──

    // ── Collision Shapes ──
    for (const auto& col : mdx.collisionShapes) {
        registerNode(col.node.objectId, col.node.parentId, false);
    }

    // ── Corn Emitters (v1200) ──
    for (const auto& corn : mdx.cornEmitters) {
        registerNode(corn.node.objectId, corn.node.parentId, false);
    }
}

int32_t MdxHierarchyMapper::irIndexForObjectId(uint32_t objectId) const {
    auto it = objectIdToIrIndex_.find(objectId);
    return (it != objectIdToIrIndex_.end()) ? it->second : -1;
}

int32_t MdxHierarchyMapper::resolveParent(uint32_t mdxParentId) const {
    if (mdxParentId == 0xFFFFFFFF) return -1;
    return irIndexForObjectId(mdxParentId);
}

} // namespace mdx_disasm
