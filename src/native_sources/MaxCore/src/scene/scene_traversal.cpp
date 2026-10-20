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

bool isSkippedHidden(INode* node) {
    return node->IsHidden() && !isHiddenLodGeoset(node);
}

// True when something below the node exports: a visible descendant the
// classifier does not ignore.
bool hasExportableDescendant(INode* node, const NodeClassifier& classifier) {
    std::string tag;
    for (int i = 0; i < node->NumberOfChildren(); ++i) {
        INode* c = node->GetChildNode(i);
        if (!c) continue;
        if (!isSkippedHidden(c) && classifier.classify(c, tag) != NodeCategory::Ignored)
            return true;
        if (hasExportableDescendant(c, classifier))
            return true;
    }
    return false;
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
    // A hidden node that something exportable hangs on (an emitter or an
    // attachment linked to a hidden bone, a ribbon on a hidden sword) stays
    // in the hierarchy as a plain helper: no geometry, no emitter of its
    // own, but its animation, which the children follow in Max. Skipping it
    // linked them to the next visible ancestor and they lost that motion.
    std::string customTag;
    NodeCategory cat = NodeCategory::Ignored;
    if (node && isSkippedHidden(node)) {
        if (hasExportableDescendant(node, classifier))
            cat = classifier.classify(node, customTag);
        if (cat == NodeCategory::Ignored) {
            int numChildren = node->NumberOfChildren();
            for (int i = 0; i < numChildren; ++i) {
                visitNode(node->GetChildNode(i), parentIndex, classifier, result);
            }
            return;
        }
        if (cat != NodeCategory::Bone) {
            cat = NodeCategory::Helper;
            customTag.clear();
        }
    } else {
        cat = classifier.classify(node, customTag);
    }

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
