// MaxCore — Bone optimizer implementation
#include "bone_optimizer.h"
#include <unordered_set>

namespace core {

void BoneOptimizer::optimize(ir::IRModel& model) {
    if (model.bones.empty()) return;

    size_t numBones = model.bones.size();
    std::vector<bool> used(numBones, false);

    // 1. Mark bones referenced by skin influences
    for (const auto& mesh : model.meshes) {
        for (const auto& vert : mesh.vertices) {
            for (const auto& inf : vert.skinInfluences) {
                if (inf.boneIndex >= 0 && static_cast<size_t>(inf.boneIndex) < numBones) {
                    used[inf.boneIndex] = true;
                }
            }
        }
    }

    // 2. Mark bones with non-empty animation
    for (const auto& anim : model.nodeAnimations) {
        // Find which bone this animation refers to
        for (size_t b = 0; b < numBones; ++b) {
            if (model.bones[b].nodeIndex == anim.nodeIndex) {
                if (!anim.translation.empty() || !anim.rotation.empty() || !anim.scale.empty()) {
                    used[b] = true;
                }
            }
        }
    }

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

    // 6. Compact bone list
    std::vector<ir::Bone> newBones;
    for (size_t i = 0; i < numBones; ++i) {
        if (used[i]) {
            auto bone = std::move(model.bones[i]);
            if (bone.parentIndex >= 0 && static_cast<size_t>(bone.parentIndex) < numBones) {
                bone.parentIndex = oldToNew[bone.parentIndex];
            }
            newBones.push_back(std::move(bone));
        }
    }
    model.bones = std::move(newBones);

    // 7. Update skin influence indices
    for (auto& mesh : model.meshes) {
        for (auto& vert : mesh.vertices) {
            for (auto& inf : vert.skinInfluences) {
                if (inf.boneIndex >= 0 && static_cast<size_t>(inf.boneIndex) < numBones) {
                    inf.boneIndex = oldToNew[inf.boneIndex];
                }
            }
            // Remove influences that point to removed bones
            vert.skinInfluences.erase(
                std::remove_if(vert.skinInfluences.begin(), vert.skinInfluences.end(),
                               [](const ir::SkinInfluence& inf) { return inf.boneIndex < 0; }),
                vert.skinInfluences.end());
        }
    }
}

} // namespace core
