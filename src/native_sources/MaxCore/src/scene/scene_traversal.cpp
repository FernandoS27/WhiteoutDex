// MaxCore — Scene traversal implementation
#include "scene_traversal.h"

namespace core {

SceneTraversal::Result SceneTraversal::traverse(Interface* gi, const NodeClassifier& classifier) {
    Result result;
    INode* root = gi->GetRootNode();
    int numChildren = root->NumberOfChildren();
    for (int i = 0; i < numChildren; ++i) {
        visitNode(root->GetChildNode(i), -1, classifier, result);
    }
    return result;
}

void SceneTraversal::visitNode(INode* node, int parentIndex,
                                const NodeClassifier& classifier, Result& result) {
    std::string customTag;
    NodeCategory cat = classifier.classify(node, customTag);

    if (cat == NodeCategory::Ignored) {
        // Still traverse children — they may be exportable
        int numChildren = node->NumberOfChildren();
        for (int i = 0; i < numChildren; ++i) {
            visitNode(node->GetChildNode(i), parentIndex, classifier, result);
        }
        return;
    }

    int32_t nodeIndex = static_cast<int32_t>(result.nodes.size());

    // SceneNode entry
    SceneNode sn;
    sn.maxNode = node;
    sn.category = cat;
    sn.customTag = customTag;
    sn.nodeIndex = nodeIndex;
    sn.parentNodeIndex = parentIndex;
    result.nodes.push_back(sn);

    // Parallel IR node entry
    ir::IRModel::Node irn;
    const MCHAR* name = node->GetName();
    if (name) {
#ifdef UNICODE
        std::wstring wname(name);
        irn.name = std::string(wname.begin(), wname.end());
#else
        irn.name = std::string(name);
#endif
    }
    irn.maxNode = node;
    irn.parentIndex = parentIndex;
    irn.worldTM = node->GetNodeTM(0);
    irn.pivotPoint = irn.worldTM.GetTrans();
    result.irNodes.push_back(std::move(irn));

    // Recurse children
    int numChildren = node->NumberOfChildren();
    for (int i = 0; i < numChildren; ++i) {
        visitNode(node->GetChildNode(i), nodeIndex, classifier, result);
    }
}

} // namespace core
