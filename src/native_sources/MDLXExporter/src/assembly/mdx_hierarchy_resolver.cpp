// MDLXExporter — MDX hierarchy resolver implementation
#include "mdx_hierarchy_resolver.h"

void MdxHierarchyResolver::resolve(const ir::IRModel& model, uint32_t version) {
    mappings_.clear();
    irToObjectId_.clear();
    nextId_ = 0;

    using NT = whiteout::mdx::Node::NodeType;

    // Phase 1: Assign IDs by type in MDX canonical order
    //          (Bones → Lights → Helpers — matches MaxScript exporter & MDX chunk order)
    // Bones first
    for (auto& bone : model.bones) {
        if (!bone.isHelper)
            assignId(bone.nodeIndex, NT::Bone);
    }

    // Lights (before helpers — canonical MDX order)
    for (auto& light : model.lights)
        assignId(light.nodeIndex, NT::Light);

    // Helpers (v800 separate, v1200 merged into bones)
    if (version < 1200) {
        for (auto& bone : model.bones) {
            if (bone.isHelper)
                assignId(bone.nodeIndex, NT::Helper);
        }
    } else {
        // v1200: helpers are just bones with different geosetId
        for (auto& bone : model.bones) {
            if (bone.isHelper)
                assignId(bone.nodeIndex, NT::Bone);
        }
    }

    // Attachments
    for (auto& attach : model.attachments)
        assignId(attach.nodeIndex, NT::Attachment);

    // Particle emitters (variant 1)
    for (auto& pe : model.particleEmitters) {
        if (pe.variant == 1)
            assignId(pe.nodeIndex, NT::ParticleEmitter);
    }

    // Particle emitters (variant 2)
    for (auto& pe : model.particleEmitters) {
        if (pe.variant == 2)
            assignId(pe.nodeIndex, NT::ParticleEmitter2);
    }

    // Ribbon emitters
    for (auto& rib : model.ribbonEmitters)
        assignId(rib.nodeIndex, NT::RibbonEmitter);

    // Event objects
    for (auto& evt : model.eventObjects)
        assignId(evt.nodeIndex, NT::EventObject);

    // Collision shapes
    for (auto& cs : model.collisionShapes)
        assignId(cs.nodeIndex, NT::CollisionShape);

    // CornEmitters (v1200 only — not exported for v800)
    if (version >= 1200) {
        for (auto& pe : model.particleEmitters) {
            if (pe.variant == 3)
                assignId(pe.nodeIndex, NT::CornEmitter);
        }
    }

    // Phase 2: Resolve parentIds
    for (auto& mapping : mappings_) {
        if (mapping.irNodeIndex < 0 ||
            mapping.irNodeIndex >= static_cast<int32_t>(model.nodes.size()))
            continue;

        int32_t irParent = model.nodes[mapping.irNodeIndex].parentIndex;
        if (irParent >= 0) {
            auto it = irToObjectId_.find(irParent);
            if (it != irToObjectId_.end())
                mapping.parentId = it->second;
        }
    }
}

uint32_t MdxHierarchyResolver::getObjectId(int32_t irNodeIndex) const {
    auto it = irToObjectId_.find(irNodeIndex);
    return (it != irToObjectId_.end()) ? it->second : whiteout::mdx::Node::NO_PARENT;
}

uint32_t MdxHierarchyResolver::getParentId(int32_t irNodeIndex) const {
    auto it = irToObjectId_.find(irNodeIndex);
    if (it == irToObjectId_.end()) return whiteout::mdx::Node::NO_PARENT;

    for (auto& m : mappings_) {
        if (m.objectId == it->second)
            return m.parentId;
    }
    return whiteout::mdx::Node::NO_PARENT;
}

uint32_t MdxHierarchyResolver::assignId(int32_t irNodeIndex,
                                          whiteout::mdx::Node::NodeType type) {
    // Don't double-assign
    auto it = irToObjectId_.find(irNodeIndex);
    if (it != irToObjectId_.end())
        return it->second;

    NodeMapping m;
    m.irNodeIndex = irNodeIndex;
    m.objectId = nextId_;
    m.type = type;
    mappings_.push_back(m);
    irToObjectId_[irNodeIndex] = nextId_;
    return nextId_++;
}
