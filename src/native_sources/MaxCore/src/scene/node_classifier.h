// MaxCore — Scene node classification system
#pragma once

#include <max.h>
#include <inode.h>
#include <maxtypes.h>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace core {

enum class NodeCategory {
    Mesh,
    Bone,
    Helper,
    Camera,
    StandardLight,
    CollisionBox,
    CollisionSphere,
    Custom,
    Ignored,
};

struct SceneNode {
    INode* maxNode = nullptr;
    NodeCategory category = NodeCategory::Ignored;
    std::string customTag;
    int32_t nodeIndex = -1;
    int32_t parentNodeIndex = -1;
};

class NodeClassifier {
public:
    // Register a custom ClassID → tag mapping (used by format backends)
    void registerClassID(Class_ID id, const std::string& tag);

    // Classify a single node. Returns category; sets outTag for Custom nodes.
    NodeCategory classify(INode* node, std::string& outTag) const;

private:
    static uint64_t packClassID(Class_ID id) {
        return (static_cast<uint64_t>(id.PartA()) << 32) | static_cast<uint64_t>(id.PartB());
    }

    std::unordered_map<uint64_t, std::string> customClassIDs_;
};

} // namespace core
