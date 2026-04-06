// MDLXExporter — MDX geoset merger implementation
#include "mdx_geoset_merger.h"
#include <unordered_map>

using namespace whiteout::mdx;

void MdxGeosetMerger::merge(std::vector<Geoset>& geosets) {
    if (geosets.size() <= 1) return;

    // Group geosets by materialId + selectionGroup
    struct Key {
        uint32_t materialId;
        uint32_t selectionGroup;
        bool operator==(const Key& o) const {
            return materialId == o.materialId && selectionGroup == o.selectionGroup;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return std::hash<uint64_t>()((static_cast<uint64_t>(k.materialId) << 32) |
                                          k.selectionGroup);
        }
    };

    std::unordered_map<Key, size_t, KeyHash> groupMap;
    std::vector<Geoset> merged;

    for (auto& geo : geosets) {
        Key key{geo.materialId, geo.selectionGroup};
        auto it = groupMap.find(key);

        if (it == groupMap.end()) {
            groupMap[key] = merged.size();
            merged.push_back(std::move(geo));
        } else {
            auto& target = merged[it->second];

            // Guard against u16 index overflow
            if (target.vertexPositions.size() + geo.vertexPositions.size() > 65535) {
                merged.push_back(std::move(geo));
                continue;
            }

            // Offset indices
            uint16_t vertexOffset = static_cast<uint16_t>(target.vertexPositions.size());

            target.vertexPositions.insert(target.vertexPositions.end(),
                geo.vertexPositions.begin(), geo.vertexPositions.end());
            target.vertexNormals.insert(target.vertexNormals.end(),
                geo.vertexNormals.begin(), geo.vertexNormals.end());

            for (auto idx : geo.faces)
                target.faces.push_back(static_cast<uint16_t>(idx + vertexOffset));

            // Remap vertex groups — offset by current number of matrix groups in target
            uint8_t groupOffset = static_cast<uint8_t>(target.matrixGroups.size());
            for (auto vg : geo.vertexGroups)
                target.vertexGroups.push_back(static_cast<uint8_t>(vg + groupOffset));

            // Merge UV sets
            size_t prevVertCount = target.vertexPositions.size() - geo.vertexPositions.size();
            for (size_t uv = 0; uv < geo.textureCoordinateSets.size(); uv++) {
                if (uv >= target.textureCoordinateSets.size())
                    target.textureCoordinateSets.resize(uv + 1);
                // Pad target UV set if it was shorter than the previous vertex count
                while (target.textureCoordinateSets[uv].size() < prevVertCount)
                    target.textureCoordinateSets[uv].push_back({0.0f, 0.0f});
                target.textureCoordinateSets[uv].insert(
                    target.textureCoordinateSets[uv].end(),
                    geo.textureCoordinateSets[uv].begin(),
                    geo.textureCoordinateSets[uv].end());
            }
            // Pad any extra target UV sets that the source geoset didn't have
            for (size_t uv = geo.textureCoordinateSets.size();
                 uv < target.textureCoordinateSets.size(); uv++) {
                target.textureCoordinateSets[uv].resize(
                    target.vertexPositions.size(), {0.0f, 0.0f});
            }

            // Merge tangents — pad to keep aligned with vertex count
            if (!geo.tangents.empty() || !target.tangents.empty()) {
                // Back-fill target if it had no tangents before this source
                target.tangents.resize(prevVertCount, {0.0f, 0.0f, 0.0f, 1.0f});
                if (!geo.tangents.empty()) {
                    target.tangents.insert(target.tangents.end(),
                        geo.tangents.begin(), geo.tangents.end());
                } else {
                    // Source has no tangents — pad with defaults
                    target.tangents.resize(target.vertexPositions.size(),
                        {0.0f, 0.0f, 0.0f, 1.0f});
                }
            }

            // Merge skin data — 8 bytes per vertex (4 bone indices + 4 weights)
            if (!geo.skinData.empty() || !target.skinData.empty()) {
                // Back-fill target if it had no skin data before this source
                target.skinData.resize(prevVertCount * 8, 0);
                if (!geo.skinData.empty()) {
                    target.skinData.insert(target.skinData.end(),
                        geo.skinData.begin(), geo.skinData.end());
                } else {
                    // Source has no skin data — pad with zeros
                    target.skinData.resize(target.vertexPositions.size() * 8, 0);
                }
            }

            // Merge matrix groups/indices
            target.matrixGroups.insert(target.matrixGroups.end(),
                geo.matrixGroups.begin(), geo.matrixGroups.end());
            target.matrixIndices.insert(target.matrixIndices.end(),
                geo.matrixIndices.begin(), geo.matrixIndices.end());

            // Update face groups
            target.faceGroups.push_back(static_cast<uint32_t>(geo.faces.size()));
            target.faceTypeGroups.push_back(4); // triangles
        }
    }

    geosets = std::move(merged);
}
