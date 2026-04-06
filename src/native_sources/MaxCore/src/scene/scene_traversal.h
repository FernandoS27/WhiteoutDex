// MaxCore — Depth-first scene graph traversal
#pragma once

#include "node_classifier.h"
#include "../core/intermediate_types.h"
#include <max.h>
#include <vector>

namespace core {

class SceneTraversal {
public:
    struct Result {
        std::vector<SceneNode> nodes;
        std::vector<ir::IRModel::Node> irNodes;
    };

    Result traverse(Interface* gi, const NodeClassifier& classifier);

private:
    void visitNode(INode* node, int parentIndex,
                   const NodeClassifier& classifier, Result& result);
};

} // namespace core
