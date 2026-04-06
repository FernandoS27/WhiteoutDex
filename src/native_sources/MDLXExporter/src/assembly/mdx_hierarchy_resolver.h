// MDLXExporter — MDX hierarchy resolver: assign objectIds, parentIds, pivot points
#pragma once

#include <core/intermediate_types.h>
#include <whiteout/models/mdx/structures.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

class MdxHierarchyResolver {
public:
    struct NodeMapping {
        int32_t irNodeIndex = -1;
        uint32_t objectId = 0;
        uint32_t parentId = whiteout::mdx::Node::NO_PARENT;
        whiteout::mdx::Node::NodeType type = whiteout::mdx::Node::NodeType::Helper;
    };

    void resolve(const ir::IRModel& model, uint32_t version);

    uint32_t getObjectId(int32_t irNodeIndex) const;
    uint32_t getParentId(int32_t irNodeIndex) const;
    const std::vector<NodeMapping>& mappings() const { return mappings_; }
    uint32_t totalNodes() const { return nextId_; }

private:
    std::vector<NodeMapping> mappings_;
    std::unordered_map<int32_t, uint32_t> irToObjectId_;
    uint32_t nextId_ = 0;

    uint32_t assignId(int32_t irNodeIndex, whiteout::mdx::Node::NodeType type);
};
