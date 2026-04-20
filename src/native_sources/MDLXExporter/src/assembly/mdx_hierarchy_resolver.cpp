// MDLXExporter — MDX hierarchy resolver implementation
// CHANGES: Added debug logging for objectId assignment and parent resolution
#include "mdx_hierarchy_resolver.h"

#include <max.h>
#ifndef MDX_DEBUG_PRINT
#define MDX_DEBUG_PRINT 1
#endif
#if MDX_DEBUG_PRINT
  #define MDX_LOG(...) DebugPrint(__VA_ARGS__)
#else
  #define MDX_LOG(...) ((void)0)
#endif

void MdxHierarchyResolver::resolve(const ir::IRModel& model, uint32_t version) {
    mappings_.clear();
    irToObjectId_.clear();
    nextId_ = 0;

    using NT = whiteout::mdx::Node::NodeType;

    MDX_LOG(_T("── HierarchyResolver::resolve (v%u) ──\n"), version);
    MDX_LOG(_T("  IR nodes: %d, bones: %d, lights: %d, attachments: %d\n"),
            (int)model.nodes.size(), (int)model.bones.size(),
            (int)model.lights.size(), (int)model.attachments.size());
    MDX_LOG(_T("  particles: %d, ribbons: %d, events: %d, collisions: %d\n"),
            (int)model.particleEmitters.size(), (int)model.ribbonEmitters.size(),
            (int)model.eventObjects.size(), (int)model.collisionShapes.size());

    // Phase 1: Assign IDs by type in MDX canonical order
    MDX_LOG(_T("  Phase 1: Assigning objectIds...\n"));

    // Bones — IMPORTANT: in v1200 (Reforged) there are NO separate helpers,
    // all bones/helpers go into the BONE chunk. The BONE chunk is written in IR
    // (tree-traversal) order, so we must ALSO assign objectIds in IR order for
    // v1200. Splitting into "real bones first, helpers later" causes file-position
    // to disagree with objectId, which breaks Retera's updateIdObjectReferences
    // (ArrayIndexOutOfBoundsException "Index N out of bounds for length N").
    int boneCount = 0;
    for (auto& bone : model.bones) {
        // v<1200: only non-helper bones here; helpers assigned later (after lights) as HELP
        // v1200: all bones (helpers stored inline as bones, preserving IR order)
        if (version < 1200 && bone.isHelper) continue;
        uint32_t id = assignId(bone.nodeIndex, NT::Bone);
        MDX_LOG(_T("    BONE  objId=%u irIdx=%d \"%S\"%s\n"),
                id, bone.nodeIndex, bone.name.c_str(),
                bone.isHelper ? _T(" [isHelper]") : _T(""));
        boneCount++;
    }
    MDX_LOG(_T("  → %d bones assigned (objId 0-%u)\n"), boneCount, nextId_ > 0 ? nextId_-1 : 0);

    // Lights
    int lightCount = 0;
    for (auto& light : model.lights) {
        uint32_t id = assignId(light.nodeIndex, NT::Light);
        MDX_LOG(_T("    LIGHT objId=%u irIdx=%d\n"), id, light.nodeIndex);
        lightCount++;
    }

    // Helpers — only for v<1200 (Classic/Reforged-Compat). For v1200 they were
    // already assigned as bones above.
    int helperCount = 0;
    if (version < 1200) {
        for (auto& bone : model.bones) {
            if (bone.isHelper) {
                uint32_t id = assignId(bone.nodeIndex, NT::Helper);
                MDX_LOG(_T("    HELP  objId=%u irIdx=%d \"%S\"\n"),
                        id, bone.nodeIndex, bone.name.c_str());
                helperCount++;
            }
        }
    }

    // Attachments
    for (auto& attach : model.attachments) {
        uint32_t id = assignId(attach.nodeIndex, NT::Attachment);
        MDX_LOG(_T("    ATCH  objId=%u irIdx=%d\n"), id, attach.nodeIndex);
    }

    // Particle emitters (variant 1)
    for (auto& pe : model.particleEmitters) {
        if (pe.variant == 1) {
            uint32_t id = assignId(pe.nodeIndex, NT::ParticleEmitter);
            MDX_LOG(_T("    PRE1  objId=%u irIdx=%d\n"), id, pe.nodeIndex);
        }
    }

    // Particle emitters (variant 2)
    for (auto& pe : model.particleEmitters) {
        if (pe.variant == 2) {
            uint32_t id = assignId(pe.nodeIndex, NT::ParticleEmitter2);
            MDX_LOG(_T("    PRE2  objId=%u irIdx=%d\n"), id, pe.nodeIndex);
        }
    }

    // CornEmitters (v1200 only) — MUST come BEFORE Ribbons to match Reforged canonical order.
    // Verified against Blizzard's arthas.mdx (v1000): PRE2=157, CORN=158, RIBB=159, EVTS=160.
    // If CORN is assigned last (after CLID), Retera's updateIdObjectReferences crashes with
    // "Index N out of bounds" because CORN's objectId ends up outside the expected range.
    if (version >= 1200) {
        for (auto& pe : model.particleEmitters) {
            if (pe.variant == 3) {
                uint32_t id = assignId(pe.nodeIndex, NT::CornEmitter);
                MDX_LOG(_T("    CORN  objId=%u irIdx=%d\n"), id, pe.nodeIndex);
            }
        }
    }

    // Ribbon emitters
    for (auto& rib : model.ribbonEmitters) {
        uint32_t id = assignId(rib.nodeIndex, NT::RibbonEmitter);
        MDX_LOG(_T("    RIBB  objId=%u irIdx=%d\n"), id, rib.nodeIndex);
    }

    // Event objects
    for (auto& evt : model.eventObjects) {
        uint32_t id = assignId(evt.nodeIndex, NT::EventObject);
        MDX_LOG(_T("    EVTS  objId=%u irIdx=%d\n"), id, evt.nodeIndex);
    }

    // Collision shapes
    for (auto& cs : model.collisionShapes) {
        uint32_t id = assignId(cs.nodeIndex, NT::CollisionShape);
        MDX_LOG(_T("    CLID  objId=%u irIdx=%d\n"), id, cs.nodeIndex);
    }

    MDX_LOG(_T("  Phase 1 complete: %u total nodes\n"), nextId_);

    // Phase 2: Resolve parentIds
    MDX_LOG(_T("  Phase 2: Resolving parentIds...\n"));
    int resolvedCount = 0, orphanCount = 0;
    for (auto& mapping : mappings_) {
        if (mapping.irNodeIndex < 0 ||
            mapping.irNodeIndex >= static_cast<int32_t>(model.nodes.size()))
        {
            MDX_LOG(_T("    ✗ objId=%u has invalid irNodeIndex=%d\n"),
                    mapping.objectId, mapping.irNodeIndex);
            orphanCount++;
            continue;
        }

        int32_t irParent = model.nodes[mapping.irNodeIndex].parentIndex;
        if (irParent >= 0) {
            auto it = irToObjectId_.find(irParent);
            if (it != irToObjectId_.end()) {
                mapping.parentId = it->second;
                resolvedCount++;
            } else {
                MDX_LOG(_T("    ⚠ objId=%u irParent=%d not found in hierarchy!\n"),
                        mapping.objectId, irParent);
                orphanCount++;
            }
        }
    }
    MDX_LOG(_T("  Phase 2 complete: %d resolved, %d orphaned/root\n"),
            resolvedCount, orphanCount);
    MDX_LOG(_T("── HierarchyResolver done ──\n\n"));
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
    if (it != irToObjectId_.end()) {
        MDX_LOG(_T("    ⚠ irIdx=%d already assigned to objId=%u (skipping)\n"),
                irNodeIndex, it->second);
        return it->second;
    }

    NodeMapping m;
    m.irNodeIndex = irNodeIndex;
    m.objectId = nextId_;
    m.type = type;
    mappings_.push_back(m);
    irToObjectId_[irNodeIndex] = nextId_;
    return nextId_++;
}
