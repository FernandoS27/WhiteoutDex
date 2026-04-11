// MaxCore — Bone hierarchy extractor implementation
#include "bone_extractor.h"
#include "../util/class_ids.h"
#include "../util/max_helpers.h"

#include <CS/BIPEXP.H>

namespace core {

BoneExtractor::BoneResult BoneExtractor::extract(
    const std::vector<SceneNode>& sceneNodes,
    const std::vector<INode*>& skinBoneNodes,
    ExportErrorReporter& reporter) {

    BoneResult result;
    std::unordered_set<INode*> addedNodes;

    // 1. Collect all explicitly classified bones and helpers
    for (const auto& sn : sceneNodes) {
        if (sn.category == NodeCategory::Bone || sn.category == NodeCategory::Helper) {
            if (addedNodes.count(sn.maxNode)) continue;
            addedNodes.insert(sn.maxNode);

            ir::Bone bone;
            const MCHAR* name = sn.maxNode->GetName();
            if (name) {
                std::wstring wname(name);
                bone.name.assign(wname.begin(), wname.end());
            }
            bone.nodeIndex = sn.nodeIndex;
            bone.type = detectBoneType(sn.maxNode);
            bone.isHelper = (sn.category == NodeCategory::Helper);
            bone.pivotPoint = sn.maxNode->GetNodeTM(0).GetTrans();
            bone.bindPose = sn.maxNode->GetNodeTM(0);
            bone.nodeFlags = collectNodeFlags(sn.maxNode);

            int32_t boneIdx = static_cast<int32_t>(result.bones.size());
            result.nodeToIndex[sn.maxNode] = boneIdx;
            result.bones.push_back(std::move(bone));
        }
    }

    // 2. Add implicit bones (referenced by Skin but not already in bone list)
    for (INode* boneNode : skinBoneNodes) {
        if (!boneNode || addedNodes.count(boneNode)) continue;
        addedNodes.insert(boneNode);

        ir::Bone bone;
        const MCHAR* name = boneNode->GetName();
        if (name) {
            std::wstring wname(name);
            bone.name.assign(wname.begin(), wname.end());
            reporter.warning(L"Implicit bone added from Skin modifier: '" + wname + L"'.");
        }
        bone.nodeIndex = -1; // Not in scene traversal
        bone.type = detectBoneType(boneNode);
        bone.isHelper = false;
        bone.pivotPoint = boneNode->GetNodeTM(0).GetTrans();
        bone.bindPose = boneNode->GetNodeTM(0);
        bone.nodeFlags = collectNodeFlags(boneNode);

        int32_t boneIdx = static_cast<int32_t>(result.bones.size());
        result.nodeToIndex[boneNode] = boneIdx;
        result.bones.push_back(std::move(bone));
    }

    // 3. Add ancestors of existing bones that aren't in the list
    //    (ensures hierarchy completeness)
    INode* root = GetCOREInterface()->GetRootNode();
    std::vector<INode*> ancestors;
    for (size_t i = 0; i < result.bones.size(); ++i) {
        // Find the INode for this bone
        INode* node = nullptr;
        for (const auto& [n, idx] : result.nodeToIndex) {
            if (idx == static_cast<int32_t>(i)) { node = n; break; }
        }
        if (!node) continue;

        // Walk up to root, collecting missing ancestors
        ancestors.clear();
        INode* parent = node->GetParentNode();
        while (parent && parent != root) {
            if (!addedNodes.count(parent)) {
                ancestors.push_back(parent);
            }
            parent = parent->GetParentNode();
        }

        // Add ancestors (from root toward leaf) so parent indices resolve correctly
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
            INode* anc = *it;
            if (addedNodes.count(anc)) continue;
            addedNodes.insert(anc);

            ir::Bone bone;
            const MCHAR* aname = anc->GetName();
            if (aname) {
                std::wstring wname(aname);
                bone.name.assign(wname.begin(), wname.end());
            }
            bone.nodeIndex = -1;
            bone.type = detectBoneType(anc);
            bone.isHelper = true;
            bone.pivotPoint = anc->GetNodeTM(0).GetTrans();
            bone.bindPose = anc->GetNodeTM(0);
            bone.nodeFlags = collectNodeFlags(anc);

            int32_t boneIdx = static_cast<int32_t>(result.bones.size());
            result.nodeToIndex[anc] = boneIdx;
            result.bones.push_back(std::move(bone));
        }
    }

    // 4. Resolve parent indices
    for (auto& [node, idx] : result.nodeToIndex) {
        INode* parent = node->GetParentNode();
        if (parent && parent != root) {
            auto pit = result.nodeToIndex.find(parent);
            if (pit != result.nodeToIndex.end()) {
                result.bones[idx].parentIndex = pit->second;
            }
        }
    }

    return result;
}

ir::BoneType BoneExtractor::detectBoneType(INode* node) const {
    if (!node) return ir::BoneType::Standard;

    // Check for Biped via TM controller ClassID
    Control* tmCtrl = node->GetTMController();
    if (tmCtrl) {
        Class_ID ctrlID = tmCtrl->ClassID();
        if (ctrlID == BIPBODY_CONTROL_CLASS_ID || ctrlID == BIPDRIVEN_CONTROL_CLASS_ID)
            return ir::BoneType::Biped;
    }

    // Check for CAT
    ObjectState os = node->EvalWorldState(0);
    if (os.obj) {
        Class_ID objID = os.obj->ClassID();
        if (objID == core_ids::CAT_PARENT_ID ||
            objID == core_ids::CAT_BONE_ID ||
            objID == core_ids::HUB_ID)
            return ir::BoneType::CAT;
    }

    // Check for IK (node has IK chain controller)
    if (tmCtrl) {
        // IK chain controllers typically have a specific interface
        if (tmCtrl->GetInterface(0x00000001) != nullptr)
            return ir::BoneType::IKAffected;
    }

    return ir::BoneType::Standard;
}

} // namespace core
