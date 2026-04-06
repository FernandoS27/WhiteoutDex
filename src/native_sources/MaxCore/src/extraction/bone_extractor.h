// MaxCore — Bone hierarchy extractor
#pragma once

#include "../core/intermediate_types.h"
#include "../scene/node_classifier.h"
#include "../util/error_reporter.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace core {

class BoneExtractor {
public:
    struct BoneResult {
        std::vector<ir::Bone> bones;
        /// Map: INode* → index in bones[]
        std::unordered_map<INode*, int32_t> nodeToIndex;
    };

    /// Extract the full bone hierarchy from classified scene nodes.
    /// Also detects implicit bones (nodes referenced by Skin but not in the bone list).
    BoneResult extract(const std::vector<SceneNode>& sceneNodes,
                       const std::vector<INode*>& skinBoneNodes,
                       ExportErrorReporter& reporter);

private:
    ir::BoneType detectBoneType(INode* node) const;
};

} // namespace core
