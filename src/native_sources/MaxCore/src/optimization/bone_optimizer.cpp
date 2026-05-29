// MaxCore — Bone optimizer implementation
// CHANGES: Step 2 now marks bones referenced by MDX content nodes
//          (lights, attachments, particles, ribbons, events, collisions,
//          cameras) instead of "has non-empty animation". This matches
//          NeoDex's approach: only nodes that are parents of content
//          survive the optimization. IK chains, standalone dummies, etc.
//          get removed because nothing references them.
#include "bone_optimizer.h"
#include <unordered_set>
#include <unordered_map>

namespace core {

void BoneOptimizer::optimize(ir::IRModel& model) {
    if (model.bones.empty()) return;

    size_t numBones = model.bones.size();
    std::vector<bool> used(numBones, false);

    // Build a fast nodeIdx → bone-array-pos lookup.
    std::unordered_map<int32_t, size_t> nodeIdxToBoneArrayPos;
    nodeIdxToBoneArrayPos.reserve(numBones);
    for (size_t b = 0; b < numBones; ++b) {
        nodeIdxToBoneArrayPos[model.bones[b].nodeIndex] = b;
    }

    // Helper: mark a bone as used by nodeIndex
    auto markUsed = [&](int32_t nodeIndex) {
        auto it = nodeIdxToBoneArrayPos.find(nodeIndex);
        if (it != nodeIdxToBoneArrayPos.end())
            used[it->second] = true;
    };

    // Helper: mark a node and all its ancestors in the bone list as used.
    // Content nodes (PE2, Light, etc.) aren't bones themselves, but their
    // PARENTS are. Walk up the irModel.nodes parent chain and mark any
    // node that appears in the bone list.
    auto markWithAncestors = [&](int32_t nodeIndex) {
        // First try the node itself (e.g. unskinned mesh parents)
        markUsed(nodeIndex);
        // Then walk up the parent chain via irModel.nodes
        int32_t ni = nodeIndex;
        while (ni >= 0 && ni < static_cast<int32_t>(model.nodes.size())) {
            int32_t parentNi = model.nodes[ni].parentIndex;
            if (parentNi < 0) break;
            markUsed(parentNi);
            ni = parentNi;
        }
    };

    // 1. Mark bones referenced by skin influences
    for (const auto& mesh : model.meshes) {
        for (const auto& vert : mesh.vertices) {
            for (const auto& inf : vert.skinInfluences) {
                markUsed(inf.boneIndex);
            }
        }
    }

    // 2. Mark bones referenced by MDX content nodes.
    //    This replaces the old "has animation" criterion which kept
    //    IK chains and other non-content nodes alive because the
    //    AnimDispatcher bakes animation on ALL nodes.
    //    Now we only keep nodes that are actually needed by MDX content.
    //    markWithAncestors walks up the parent chain so that parent
    //    helpers (Point001, Dummy004, etc.) of content nodes are also kept.
    for (const auto& light : model.lights)
        markWithAncestors(light.nodeIndex);
    for (const auto& attach : model.attachments)
        markWithAncestors(attach.nodeIndex);
    for (const auto& pe : model.particleEmitters)
        markWithAncestors(pe.nodeIndex);
    for (const auto& rib : model.ribbonEmitters)
        markWithAncestors(rib.nodeIndex);
    for (const auto& evt : model.eventObjects)
        markWithAncestors(evt.nodeIndex);
    for (const auto& cs : model.collisionShapes)
        markWithAncestors(cs.nodeIndex);

    // 3. Mark ancestors of all used bones
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < numBones; ++i) {
            if (used[i] && model.bones[i].parentIndex >= 0) {
                size_t parent = static_cast<size_t>(model.bones[i].parentIndex);
                if (parent < numBones && !used[parent]) {
                    used[parent] = true;
                    changed = true;
                }
            }
        }
    }

    // Check if any bones can be removed
    size_t removeCount = 0;
    for (size_t i = 0; i < numBones; ++i) {
        if (!used[i]) removeCount++;
    }
    if (removeCount == 0) return;

    // 4. Build mapping from old index to new index
    std::vector<int32_t> oldToNew(numBones, -1);
    int32_t newIdx = 0;
    for (size_t i = 0; i < numBones; ++i) {
        if (used[i]) {
            oldToNew[i] = newIdx++;
        }
    }

    // 5. Reparent children of removed bones
    for (size_t i = 0; i < numBones; ++i) {
        if (used[i] && model.bones[i].parentIndex >= 0) {
            // Walk up parent chain until we find a used parent
            int32_t pi = model.bones[i].parentIndex;
            while (pi >= 0 && !used[pi]) {
                pi = model.bones[pi].parentIndex;
            }
            model.bones[i].parentIndex = (pi >= 0) ? oldToNew[pi] : -1;
        }
    }

    // 6. Compact bone list.
    //
    // CRITICAL: do NOT remap parentIndex here. Step 5 above already
    // walked up the parent chain through removed ancestors and wrote
    // the new (already-compacted) parentIndex via oldToNew[pi].
    // Applying oldToNew a second time on that already-remapped value
    // produces a double-shift that scrambles the hierarchy.
    std::vector<ir::Bone> newBones;
    for (size_t i = 0; i < numBones; ++i) {
        if (used[i]) {
            newBones.push_back(std::move(model.bones[i]));
        }
    }
    model.bones = std::move(newBones);

    // 7. Skin influence indices are intentionally NOT modified.
    //
    // ir::SkinInfluence::boneIndex is populated by the disassembler via
    // hierarchy_.irIndexForObjectId() — that returns a nodeIndex (Index
    // into IRModel::nodes), NOT a bone-array-pos. The original code here
    // applied oldToNew[inf.boneIndex], but oldToNew is indexed by
    // bone-array-pos, so the remap mis-mapped influences and additionally
    // could mark valid influences as -1 when bones[N] happened to be a
    // removed bone (independent of whether nodeIndex N was actually
    // affected). Concrete failure: Crystal Golem Geoset_3 lost one skin
    // bone (6 → 5) producing wrong weights on the upper body.
    //
    // Since this optimizer only compacts IRModel::bones (the array) and
    // does NOT touch IRModel::nodes, skin influences remain valid as-is:
    // they refer to nodes that still exist at the same nodeIndex.
}

} // namespace core
