// MDLXExporter — MDX geoset merger
#pragma once

#include <core/intermediate_types.h>
#include <whiteout/models/mdx/structures.h>
#include <cstdint>
#include <vector>

class MdxGeosetMerger {
public:
    // Merge compatible geosets: same material + selection group + geoset
    // animation signature.
    //
    // `animSignatures[i]` is a content hash of the GeosetAnim bound to input
    // geoset i (0 = no geoset animation). Geosets with differing signatures
    // never merge — otherwise per-geoset visibility/color animation would
    // collapse onto one merged geoset (e.g. four sword geosets sharing a
    // material but with different KGAO tracks all became one always-broken
    // geoset). An empty vector treats every geoset as signature 0.
    //
    // `outRemap`, when non-null, receives the old-index → new-index mapping
    // (size = input geoset count) so the caller can rebind
    // GeosetAnimation::geosetId and other geoset back-references.
    void merge(std::vector<whiteout::mdx::Geoset>& geosets,
               const std::vector<uint64_t>& animSignatures = {},
               std::vector<uint32_t>* outRemap = nullptr);
};
