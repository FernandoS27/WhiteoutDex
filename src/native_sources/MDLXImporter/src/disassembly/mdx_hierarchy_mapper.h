// MDLXImporter — Map MDX node ObjectIDs to IR hierarchy
#pragma once

#include <core/intermediate_types.h>
#include <whiteout/models/mdx/structures.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mdx_disasm {

/// Maps MDX ObjectID space into a flat IR node index array.
///
/// MDX ObjectID ordering:
///   0..B-1           Bones
///   B..B+H-1         Helpers (v800 only; v1200 packs helpers into bones with flag 0x100)
///   B+H..            Lights, Attachments, ParticleEmitters, ...
///
/// The mapper assigns sequential IR node indices and resolves parent references.
class MdxHierarchyMapper {
public:
    struct MappedNode {
        int32_t irIndex = -1;     // Index in ir::IRModel::nodes
        int32_t irParent = -1;    // Parent's IR index (-1 for root)
        uint32_t mdxObjectId = 0;
        bool isHelper = false;
    };

    /// Build the hierarchy map from an MDX model.
    void build(const whiteout::mdx::Model& mdx);

    /// Look up IR node index for a given MDX ObjectID.
    int32_t irIndexForObjectId(uint32_t objectId) const;

    /// Look up IR parent index for a given MDX parentId.
    int32_t resolveParent(uint32_t mdxParentId) const;

    /// Total number of mapped nodes.
    int32_t nodeCount() const { return static_cast<int32_t>(nodes_.size()); }

    /// Access all mapped nodes.
    const std::vector<MappedNode>& nodes() const { return nodes_; }

private:
    std::vector<MappedNode> nodes_;
    std::unordered_map<uint32_t, int32_t> objectIdToIrIndex_;
};

} // namespace mdx_disasm
