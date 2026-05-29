// MaxCore — Bone hierarchy extractor implementation
#include "bone_extractor.h"
#include "../util/class_ids.h"
#include "../util/max_helpers.h"

#include <CS/BIPEXP.H>
#include <algorithm>
#include <fstream>
#include <string>
#include <windows.h>

// Bone_Main focused log — writes to %TEMP%\mdlx_bone_main.log in APPEND mode.
// Each TU has its own static ofstream; file is truncated at export start.
static std::ofstream& bmLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_bone_main.log";
        log.open(path, std::ios::app);
    }
    return log;
}
#define BMLOG   bmLog()
#define BMFLUSH bmLog().flush()

namespace core {

BoneExtractor::BoneResult BoneExtractor::extract(
    const std::vector<SceneNode>& sceneNodes,
    const std::vector<INode*>& skinBoneNodes,
    ExportErrorReporter& reporter) {

    BoneResult result;
    std::unordered_set<INode*> addedNodes;

    // Build a fast lookup set for skin-referenced bones
    std::unordered_set<INode*> skinRefSet(skinBoneNodes.begin(), skinBoneNodes.end());

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

            // v800 bone-vs-helper classification — matches NeoDex semantics
            // exactly. NeoDex's rule (NeoDexSceneParser.ms LoadObjects /
            // processSkinning, lines 427-469) is:
            //
            //   • Any node referenced by the Skin modifier (regardless of
            //     class — Dummy, Bone, Biped, Point Helper, …) is loaded
            //     as a Wc3Bone → BONE chunk.
            //   • Every other node is loaded as a Wc3Helper → HELP chunk.
            //
            // Class does NOT enter the decision. This matters for FBX-
            // imported skeletons, where the bones are typically Dummy
            // objects (Helper SuperClass). The previous logic explicitly
            // marked Helper-class nodes as helpers BEFORE checking skin
            // refs, which mis-routed every FBX-Dummy bone into the HELP
            // chunk and produced the empty-BONE-chunk export the user
            // observed.
            //
            // For v1200 (Reforged) the model builder writes everything to
            // the BONE chunk regardless, so this distinction only matters
            // for v800.
            bool isSkinReferenced = (skinRefSet.find(sn.maxNode) != skinRefSet.end());
            bone.isHelper = !isSkinReferenced;

            bone.pivotPoint = sn.maxNode->GetNodeTM(0).GetTrans();
            bone.bindPose = sn.maxNode->GetNodeTM(0);
            bone.nodeFlags = collectNodeFlags(sn.maxNode);

            // ── BONE_MAIN focused debug ───────────────────────────
            if (bone.name == "Bone_Main") {
                BMLOG << "====================================\n";
                BMLOG << "[bone_extractor] Bone_Main extraction\n";
                BMLOG << "====================================\n";
                BMLOG << "  nodeIndex=" << bone.nodeIndex << "\n";
                BMLOG << "  isHelper=" << bone.isHelper << "\n";
                BMLOG << "  GetNodeTM(0).GetTrans() (Max-space) = ("
                     << bone.pivotPoint.x << ", "
                     << bone.pivotPoint.y << ", "
                     << bone.pivotPoint.z << ")\n";

                // Also read the position controller directly to see what
                // the key value is. If ORT=constant with 1 key, Max's
                // GetNodeTM at t=0 returns the key value, not a 'rest pose'.
                Control* tmCtrl = sn.maxNode->GetTMController();
                if (tmCtrl) {
                    Control* posCtrl = tmCtrl->GetPositionController();
                    Control* rotCtrl = tmCtrl->GetRotationController();
                    if (posCtrl) {
                        int npk = posCtrl->NumKeys();
                        BMLOG << "  posCtrl class=0x" << std::hex
                             << posCtrl->ClassID().PartA() << std::dec
                             << " numKeys=" << npk
                             << " before=" << posCtrl->GetORT(ORT_BEFORE)
                             << " after=" << posCtrl->GetORT(ORT_AFTER) << "\n";
                        for (int i = 0; i < npk && i < 5; ++i) {
                            TimeValue t = posCtrl->GetKeyTime(i);
                            Point3 v(0.0f, 0.0f, 0.0f);
                            Interval iv = FOREVER;
                            posCtrl->GetValue(t, &v, iv);
                            BMLOG << "    posKey[" << i << "] t=" << t
                                 << " val=(" << v.x << ", " << v.y << ", " << v.z << ")\n";
                        }
                    }
                    if (rotCtrl) {
                        int nrk = rotCtrl->NumKeys();
                        BMLOG << "  rotCtrl class=0x" << std::hex
                             << rotCtrl->ClassID().PartA() << std::dec
                             << " numKeys=" << nrk
                             << " before=" << rotCtrl->GetORT(ORT_BEFORE)
                             << " after=" << rotCtrl->GetORT(ORT_AFTER) << "\n";
                        for (int i = 0; i < nrk && i < 5; ++i) {
                            TimeValue t = rotCtrl->GetKeyTime(i);
                            Quat q(0.0f, 0.0f, 0.0f, 1.0f);
                            Interval iv = FOREVER;
                            rotCtrl->GetValue(t, &q, iv);
                            BMLOG << "    rotKey[" << i << "] t=" << t
                                 << " quat=(" << q.x << ", " << q.y << ", "
                                 << q.z << ", " << q.w << ")\n";
                        }
                    }
                }
                BMLOG << "  Recorded as pivotPoint (Max-space), will be transformed later\n\n";
                BMFLUSH;
            }

            int32_t boneIdx = static_cast<int32_t>(result.bones.size());
            result.nodeToIndex[sn.maxNode] = boneIdx;
            result.bones.push_back(std::move(bone));
        }
    }

    // 2. Add implicit bones (referenced by Skin but not already in bone list)
    //    These are by definition skin-referenced → always BONE, never helper.
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
        bone.isHelper = false; // Skin-referenced → always BONE
        bone.pivotPoint = boneNode->GetNodeTM(0).GetTrans();
        bone.bindPose = boneNode->GetNodeTM(0);
        bone.nodeFlags = collectNodeFlags(boneNode);

        int32_t boneIdx = static_cast<int32_t>(result.bones.size());
        result.nodeToIndex[boneNode] = boneIdx;
        result.bones.push_back(std::move(bone));
    }

    // 3. Add ancestors of existing bones that aren't in the list
    //    (ensures hierarchy completeness)
    //    Ancestors are not skin-referenced → always HELPER.
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
            // Same skin-based rule as Stage 1: only nodes referenced by a
            // Skin modifier go to the BONE chunk. Ancestors that happen to
            // be skin-referenced (rare but possible in deeply-nested rigs)
            // stay bones; the rest become helpers.
            bone.isHelper = (skinRefSet.find(anc) == skinRefSet.end());
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

    // Check for CAT (ClassID + ClassName fallback for version compatibility)
    ObjectState os = node->EvalWorldState(0);
    if (os.obj) {
        Class_ID objID = os.obj->ClassID();
        if (objID == core_ids::CAT_PARENT_ID ||
            objID == core_ids::CAT_BONE_ID ||
            objID == core_ids::HUB_ID)
            return ir::BoneType::CAT;

        // ClassName fallback — CAT ClassIDs can change between Max versions
        MSTR classNameStr;
        os.obj->GetClassName(classNameStr);
        const MCHAR* className = classNameStr.data();
        if (className) {
            if (_wcsicmp(className, L"CATBone") == 0 ||
                _wcsicmp(className, L"HubObject") == 0 ||
                _wcsicmp(className, L"CATParent") == 0)
                return ir::BoneType::CAT;
        }
    }

    // Check for IK (node has IK chain controller)
    if (tmCtrl) {
        if (tmCtrl->GetInterface(0x00000001) != nullptr)
            return ir::BoneType::IKAffected;
    }

    return ir::BoneType::Standard;
}

} // namespace core
