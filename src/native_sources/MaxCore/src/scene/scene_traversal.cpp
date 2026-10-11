// MaxCore — Scene traversal implementation
#include "scene_traversal.h"
#include <wdx_text.h>
#include "../util/max_helpers.h"
#include <ilayer.h>

namespace core {

namespace {
bool isHiddenLodGeoset(INode* node) {
    int lod = 0;
    if (!node->GetUserPropInt(_T("Wc3GeosetLod"), lod) || lod <= 0)
        return false;
    auto* layer = static_cast<ILayer*>(node->GetReference(NODE_LAYER_REF));
    return layer && layer->IsHidden();
}
} // namespace

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
    // Skip hidden nodes (matches NeoDex IsIgnorable: obj.isHidden).
    // Children of hidden nodes are still traversed because a visible
    // child of a hidden parent should still export — same as the
    // existing NodeCategory::Ignored behaviour below. The child's
    // parentIndex is forwarded so the hierarchy stays flat over the
    // skipped node.
    // Exception: the importer puts the lower detail levels of an HD model
    // (geosets with lod > 0) into hidden "Geosets_LOD<N>" layers only to
    // keep the viewport readable. Such a mesh, hidden by its layer, still
    // belongs to the model - without it the export loses every LOD and the
    // bones that carry them. A mesh hidden while its layer shows is skipped.
    if (node && node->IsHidden() && !isHiddenLodGeoset(node)) {
        int numChildren = node->NumberOfChildren();
        for (int i = 0; i < numChildren; ++i) {
            visitNode(node->GetChildNode(i), parentIndex, classifier, result);
        }
        return;
    }

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
        // MDX bytes: the Windows code page, else UTF-8 (wdx_text.h) -
        // cutting each character to a byte garbled any non-ASCII name
        irn.name = wdx::text::wideToMdx(name);
#else
        irn.name = std::string(name);
#endif
    }
    irn.maxNode = node;
    irn.parentIndex = parentIndex;
    irn.worldTM = node->GetNodeTM(0);
    irn.pivotPoint = irn.worldTM.GetTrans();

    irn.nodeFlags = collectNodeFlags(node);

    result.irNodes.push_back(std::move(irn));

    // Recurse children
    int numChildren = node->NumberOfChildren();
    for (int i = 0; i < numChildren; ++i) {
        visitNode(node->GetChildNode(i), nodeIndex, classifier, result);
    }
}

} // namespace core
