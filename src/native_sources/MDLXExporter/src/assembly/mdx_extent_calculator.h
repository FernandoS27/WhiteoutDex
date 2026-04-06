// MDLXExporter — MDX extent calculator
#pragma once

#include <core/intermediate_types.h>
#include <whiteout/models/mdx/types.h>
#include <whiteout/models/mdx/structures.h>

class MdxExtentCalculator {
public:
    // Compute per-sequence extents for each geoset and global model extent
    void compute(const ir::IRModel& ir, whiteout::mdx::Model& model);

    // Compute extent from a set of positions
    static whiteout::mdx::Extent fromPositions(const whiteout::Vector3f* positions,
                                                size_t count);
};
