// MDLXExporter — MDX geoset merger implementation
// CHANGES: Added debug logging for merge decisions and results
#include "mdx_geoset_merger.h"
#include <unordered_map>

#include <max.h>
#ifndef MDX_DEBUG_PRINT
#define MDX_DEBUG_PRINT 1
#endif
#if MDX_DEBUG_PRINT
  #define MDX_LOG(...) DebugPrint(__VA_ARGS__)
#else
  #define MDX_LOG(...) ((void)0)
#endif

using namespace whiteout::mdx;

void MdxGeosetMerger::merge(std::vector<Geoset>& geosets,
                            const std::vector<uint64_t>& animSignatures,
                            std::vector<uint32_t>* outRemap) {
    MDX_LOG(_T("── GeosetMerger::merge ──\n"));
    MDX_LOG(_T("  Input: %d geosets\n"), (int)geosets.size());

    if (geosets.size() <= 1) {
        MDX_LOG(_T("  Nothing to merge (0 or 1 geosets)\n"));
        MDX_LOG(_T("── GeosetMerger done ──\n\n"));
        if (outRemap) {
            outRemap->clear();
            for (size_t i = 0; i < geosets.size(); i++)
                outRemap->push_back(static_cast<uint32_t>(i));
        }
        return;
    }

    // Log input geosets
    for (size_t i = 0; i < geosets.size(); i++) {
        MDX_LOG(_T("  geo[%d] mat=%u sel=%u verts=%d faces=%d\n"),
                (int)i, geosets[i].materialId, geosets[i].selectionGroup,
                (int)geosets[i].vertexPositions.size(), (int)geosets[i].faces.size());
    }

    struct Key {
        uint32_t materialId;
        uint32_t selectionGroup;
        uint64_t animSig;
        bool operator==(const Key& o) const {
            return materialId == o.materialId && selectionGroup == o.selectionGroup &&
                   animSig == o.animSig;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            size_t h = std::hash<uint64_t>()((static_cast<uint64_t>(k.materialId) << 32) |
                                              k.selectionGroup);
            return h ^ (std::hash<uint64_t>()(k.animSig) * 0x9E3779B97F4A7C15ull);
        }
    };

    std::unordered_map<Key, size_t, KeyHash> groupMap;
    std::vector<Geoset> merged;
    std::vector<uint32_t> remap(geosets.size(), 0);
    int mergeCount = 0;

    for (size_t gi = 0; gi < geosets.size(); gi++) {
        auto& geo = geosets[gi];
        uint64_t sig = (gi < animSignatures.size()) ? animSignatures[gi] : 0;
        Key key{geo.materialId, geo.selectionGroup, sig};
        auto it = groupMap.find(key);

        if (it == groupMap.end()) {
            remap[gi] = static_cast<uint32_t>(merged.size());
            groupMap[key] = merged.size();
            merged.push_back(std::move(geo));
        } else {
            auto& target = merged[it->second];

            if (target.vertexPositions.size() + geo.vertexPositions.size() > 65535) {
                MDX_LOG(_T("  ⚠ Overflow: can't merge (would exceed 65535 verts), keeping separate\n"));
                remap[gi] = static_cast<uint32_t>(merged.size());
                merged.push_back(std::move(geo));
                continue;
            }
            remap[gi] = static_cast<uint32_t>(it->second);

            MDX_LOG(_T("  Merging into group %d (mat=%u): +%d verts, +%d faces\n"),
                    (int)it->second, key.materialId,
                    (int)geo.vertexPositions.size(), (int)geo.faces.size());
            mergeCount++;

            uint16_t vertexOffset = static_cast<uint16_t>(target.vertexPositions.size());

            target.vertexPositions.insert(target.vertexPositions.end(),
                geo.vertexPositions.begin(), geo.vertexPositions.end());
            target.vertexNormals.insert(target.vertexNormals.end(),
                geo.vertexNormals.begin(), geo.vertexNormals.end());

            for (auto idx : geo.faces)
                target.faces.push_back(static_cast<uint16_t>(idx + vertexOffset));

            uint8_t groupOffset = static_cast<uint8_t>(target.matrixGroups.size());
            for (auto vg : geo.vertexGroups)
                target.vertexGroups.push_back(static_cast<uint8_t>(vg + groupOffset));

            size_t prevVertCount = target.vertexPositions.size() - geo.vertexPositions.size();
            for (size_t uv = 0; uv < geo.textureCoordinateSets.size(); uv++) {
                if (uv >= target.textureCoordinateSets.size())
                    target.textureCoordinateSets.resize(uv + 1);
                while (target.textureCoordinateSets[uv].size() < prevVertCount)
                    target.textureCoordinateSets[uv].push_back({0.0f, 0.0f});
                target.textureCoordinateSets[uv].insert(
                    target.textureCoordinateSets[uv].end(),
                    geo.textureCoordinateSets[uv].begin(),
                    geo.textureCoordinateSets[uv].end());
            }
            for (size_t uv = geo.textureCoordinateSets.size();
                 uv < target.textureCoordinateSets.size(); uv++) {
                target.textureCoordinateSets[uv].resize(
                    target.vertexPositions.size(), {0.0f, 0.0f});
            }

            if (!geo.tangents.empty() || !target.tangents.empty()) {
                target.tangents.resize(prevVertCount, {0.0f, 0.0f, 0.0f, 1.0f});
                if (!geo.tangents.empty()) {
                    target.tangents.insert(target.tangents.end(),
                        geo.tangents.begin(), geo.tangents.end());
                } else {
                    target.tangents.resize(target.vertexPositions.size(),
                        {0.0f, 0.0f, 0.0f, 1.0f});
                }
            }

            if (!geo.skinData.empty() || !target.skinData.empty()) {
                target.skinData.resize(prevVertCount * 8, 0);
                if (!geo.skinData.empty()) {
                    target.skinData.insert(target.skinData.end(),
                        geo.skinData.begin(), geo.skinData.end());
                } else {
                    target.skinData.resize(target.vertexPositions.size() * 8, 0);
                }
            }

            target.matrixGroups.insert(target.matrixGroups.end(),
                geo.matrixGroups.begin(), geo.matrixGroups.end());
            target.matrixIndices.insert(target.matrixIndices.end(),
                geo.matrixIndices.begin(), geo.matrixIndices.end());

            target.faceGroups.push_back(static_cast<uint32_t>(geo.faces.size()));
            target.faceTypeGroups.push_back(4);
        }
    }

    geosets = std::move(merged);
    if (outRemap) *outRemap = std::move(remap);

    MDX_LOG(_T("  Output: %d geosets (%d merges performed)\n"),
            (int)geosets.size(), mergeCount);
    for (size_t i = 0; i < geosets.size(); i++) {
        MDX_LOG(_T("  result[%d] mat=%u verts=%d faces=%d matGroups=%d\n"),
                (int)i, geosets[i].materialId,
                (int)geosets[i].vertexPositions.size(),
                (int)geosets[i].faces.size(),
                (int)geosets[i].matrixGroups.size());
    }
    MDX_LOG(_T("── GeosetMerger done ──\n\n"));
}
