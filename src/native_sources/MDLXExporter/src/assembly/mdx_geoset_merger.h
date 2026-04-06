// MDLXExporter — MDX geoset merger
#pragma once

#include <core/intermediate_types.h>
#include <whiteout/models/mdx/structures.h>
#include <vector>

class MdxGeosetMerger {
public:
    // Merge compatible geosets (same material + geoset animation)
    void merge(std::vector<whiteout::mdx::Geoset>& geosets);
};
