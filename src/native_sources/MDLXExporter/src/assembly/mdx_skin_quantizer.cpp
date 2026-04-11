// MDLXExporter — v800 skin weight quantizer implementation
// CHANGES: Added debug logging for weight extraction, relaxation, and group assignment
#include "mdx_skin_quantizer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_set>

#include <max.h>
#ifndef MDX_DEBUG_PRINT
#define MDX_DEBUG_PRINT 1
#endif
#if MDX_DEBUG_PRINT
  #define MDX_LOG(...) DebugPrint(__VA_ARGS__)
#else
  #define MDX_LOG(...) ((void)0)
#endif

// ─────────────────────────────────────────────────────────────
bool MdxSkinQuantizer::SlotAssignment::operator==(const SlotAssignment& o) const {
    if (count != o.count) return false;
    for (int i = 0; i < count; ++i)
        if (boneIds[i] != o.boneIds[i]) return false;
    return true;
}

bool MdxSkinQuantizer::SlotAssignment::operator<(const SlotAssignment& o) const {
    if (count != o.count) return count < o.count;
    for (int i = 0; i < count; ++i) {
        if (boneIds[i] != o.boneIds[i]) return boneIds[i] < o.boneIds[i];
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
void MdxSkinQuantizer::mergeWeight(VertexWeights& w, uint32_t boneId, float weight) {
    for (auto& bw : w) {
        if (bw.boneId == boneId) { bw.weight += weight; return; }
    }
    w.push_back({boneId, weight});
}

void MdxSkinQuantizer::normalize(VertexWeights& w) {
    float sum = 0.0f;
    for (auto& bw : w) sum += bw.weight;
    if (sum > 1e-7f) {
        float inv = 1.0f / sum;
        for (auto& bw : w) bw.weight *= inv;
    }
}

void MdxSkinQuantizer::prune(VertexWeights& w, float threshold) {
    w.erase(std::remove_if(w.begin(), w.end(),
                           [threshold](const BoneWeight& bw) {
                               return bw.weight < threshold;
                           }),
            w.end());
}

void MdxSkinQuantizer::sharpen(VertexWeights& w, float temperature) {
    if (w.empty()) return;
    float maxW = 0.0f;
    for (auto& bw : w) maxW = std::max(maxW, bw.weight);

    float invT = 1.0f / std::max(temperature, 1e-6f);
    float sumExp = 0.0f;
    for (auto& bw : w) {
        bw.weight = std::exp((bw.weight - maxW) * invT);
        sumExp += bw.weight;
    }
    if (sumExp > 1e-7f) {
        float inv = 1.0f / sumExp;
        for (auto& bw : w) bw.weight *= inv;
    }
}

// ─────────────────────────────────────────────────────────────
std::vector<MdxSkinQuantizer::VertexWeights>
MdxSkinQuantizer::extractWeights(const ir::Mesh& mesh,
                                  const MdxHierarchyResolver& hierarchy) const {
    using wdx = whiteout::mdx::Node;
    std::vector<VertexWeights> out(mesh.vertices.size());

    int unmappedCount = 0;

    for (size_t vi = 0; vi < mesh.vertices.size(); ++vi) {
        auto& vert = mesh.vertices[vi];
        auto& vw = out[vi];

        for (auto& inf : vert.skinInfluences) {
            if (inf.boneIndex < 0 || inf.weight <= 0.0f) continue;
            uint32_t objId = hierarchy.getObjectId(inf.boneIndex);
            if (objId == wdx::NO_PARENT) {
                unmappedCount++;
                continue;
            }
            mergeWeight(vw, objId, inf.weight);
        }

        std::sort(vw.begin(), vw.end(),
                  [](const BoneWeight& a, const BoneWeight& b) {
                      return a.weight > b.weight;
                  });
        normalize(vw);
    }

    MDX_LOG(_T("── SkinQuantizer::extractWeights ──\n"));
    MDX_LOG(_T("  %d vertices, %d unmapped bone references\n"),
            (int)mesh.vertices.size(), unmappedCount);

    // Debug: dump first 5 vertices' weights
    for (size_t vi = 0; vi < std::min(mesh.vertices.size(), size_t(5)); vi++) {
        MDX_LOG(_T("  vert[%d]:"), (int)vi);
        for (auto& bw : out[vi])
            MDX_LOG(_T(" bone%u=%.3f"), bw.boneId, bw.weight);
        MDX_LOG(_T("\n"));
    }

    return out;
}

// ─────────────────────────────────────────────────────────────
std::vector<std::vector<uint32_t>>
MdxSkinQuantizer::buildAdjacency(const std::vector<uint32_t>& indices,
                                  size_t vertexCount) const {
    std::vector<std::unordered_set<uint32_t>> adj(vertexCount);

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a < vertexCount && b < vertexCount && c < vertexCount) {
            adj[a].insert(b); adj[a].insert(c);
            adj[b].insert(a); adj[b].insert(c);
            adj[c].insert(a); adj[c].insert(b);
        }
    }

    std::vector<std::vector<uint32_t>> result(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i)
        result[i].assign(adj[i].begin(), adj[i].end());

    MDX_LOG(_T("  Adjacency: %d tris, avg %.1f neighbors/vert\n"),
            (int)(indices.size() / 3),
            vertexCount > 0 ? (float)indices.size() / vertexCount : 0.0f);

    return result;
}

// ─────────────────────────────────────────────────────────────
void MdxSkinQuantizer::relax(
    std::vector<VertexWeights>& weights,
    const std::vector<ir::Vertex>& vertices,
    const std::vector<std::vector<uint32_t>>& adjacency) const
{
    const size_t N = weights.size();
    if (N == 0) return;

    MDX_LOG(_T("  Relaxation: %d iterations, temp %.2f→%.2f, alpha=%.2f\n"),
            kRelaxIterations, kTempStart, kTempEnd, kRelaxAlpha);

    std::vector<VertexWeights> next(N);

    for (int iter = 0; iter < kRelaxIterations; ++iter) {
        float t = (kRelaxIterations > 1)
                      ? static_cast<float>(iter) / (kRelaxIterations - 1)
                      : 1.0f;
        float temperature = kTempStart * (1.0f - t) + kTempEnd * t;

        for (size_t vi = 0; vi < N; ++vi) {
            VertexWeights blended;
            for (auto& bw : weights[vi])
                mergeWeight(blended, bw.boneId, bw.weight * (1.0f - kRelaxAlpha));

            float totalInvDist = 0.0f;
            const auto& neighbors = adjacency[vi];
            Point3 posV = vertices[vi].position;

            for (uint32_t ni : neighbors) {
                Point3 delta = posV - vertices[ni].position;
                float dist = Length(delta);
                float invDist = 1.0f / std::max(dist, 1e-6f);
                totalInvDist += invDist;
                for (auto& bw : weights[ni])
                    mergeWeight(blended, bw.boneId, bw.weight * invDist);
            }

            if (totalInvDist > 1e-7f && !neighbors.empty()) {
                float neighborSum = 0.0f;
                float ownSum = 0.0f;
                for (auto& bw : blended) neighborSum += bw.weight;
                for (auto& bw : weights[vi]) ownSum += bw.weight;
                ownSum *= (1.0f - kRelaxAlpha);
                float rawNeighborSum = neighborSum - ownSum;
                if (rawNeighborSum > 1e-7f) {
                    VertexWeights corrected;
                    for (auto& bw : weights[vi])
                        mergeWeight(corrected, bw.boneId, bw.weight * (1.0f - kRelaxAlpha));
                    for (uint32_t ni : neighbors) {
                        Point3 d = posV - vertices[ni].position;
                        float dist = Length(d);
                        float invDist = 1.0f / std::max(dist, 1e-6f);
                        float w = (invDist / totalInvDist) * kRelaxAlpha;
                        for (auto& bw : weights[ni])
                            mergeWeight(corrected, bw.boneId, bw.weight * w);
                    }
                    blended = std::move(corrected);
                }
            }

            normalize(blended);
            sharpen(blended, temperature);
            prune(blended, kMinWeight);
            normalize(blended);

            std::sort(blended.begin(), blended.end(),
                      [](const BoneWeight& a, const BoneWeight& b) {
                          return a.weight > b.weight;
                      });

            if (blended.size() > kMaxSlots)
                blended.resize(kMaxSlots);
            normalize(blended);

            next[vi] = std::move(blended);
        }

        weights.swap(next);

        // Debug: after each iteration, count unique bone combos
        std::map<std::vector<uint32_t>, int> combos;
        for (auto& w : weights) {
            std::vector<uint32_t> ids;
            for (auto& bw : w) ids.push_back(bw.boneId);
            combos[ids]++;
        }
        MDX_LOG(_T("    iter %d: temp=%.3f, %d unique bone combos\n"),
                iter, temperature, (int)combos.size());
    }
}

// ─────────────────────────────────────────────────────────────
MdxSkinQuantizer::SlotAssignment
MdxSkinQuantizer::assignSlots(const VertexWeights& weights) const {
    if (weights.empty()) {
        SlotAssignment sa;
        sa.boneIds[0] = 0;
        sa.count = 1;
        return sa;
    }

    int numBones = static_cast<int>(std::min(weights.size(), size_t(kMaxSlots)));

    SlotAssignment sa;
    sa.count = numBones;
    for (int i = 0; i < numBones; ++i)
        sa.boneIds[i] = weights[i].boneId;

    std::sort(sa.boneIds, sa.boneIds + sa.count);
    return sa;
}

// ─────────────────────────────────────────────────────────────
MdxSkinQuantizer::Result
MdxSkinQuantizer::buildResult(const std::vector<SlotAssignment>& assignments) const {
    std::map<SlotAssignment, uint8_t> groupMap;
    std::vector<SlotAssignment> groupList;

    for (auto& sa : assignments) {
        if (groupMap.find(sa) == groupMap.end()) {
            uint8_t idx = static_cast<uint8_t>(groupList.size());
            groupMap[sa] = idx;
            groupList.push_back(sa);
        }
    }

    MDX_LOG(_T("  buildResult: %d unique matrix groups from %d vertices\n"),
            (int)groupList.size(), (int)assignments.size());

    if (groupList.size() > kMaxGroups) {
        MDX_LOG(_T("  ⚠ Exceeds %d groups! Merging...\n"), kMaxGroups);
        std::vector<int> usage(groupList.size(), 0);
        for (auto& sa : assignments)
            usage[groupMap[sa]]++;

        while (groupList.size() > kMaxGroups) {
            int minIdx = 0;
            for (size_t i = 1; i < groupList.size(); ++i) {
                if (usage[i] < usage[minIdx]) minIdx = static_cast<int>(i);
            }

            int bestMerge = (minIdx == 0) ? 1 : 0;
            int bestOverlap = -1;
            for (size_t i = 0; i < groupList.size(); ++i) {
                if (static_cast<int>(i) == minIdx) continue;
                int overlap = 0;
                for (int a = 0; a < groupList[minIdx].count; ++a)
                    for (int b = 0; b < groupList[i].count; ++b)
                        if (groupList[minIdx].boneIds[a] == groupList[i].boneIds[b])
                            ++overlap;
                if (overlap > bestOverlap) {
                    bestOverlap = overlap;
                    bestMerge = static_cast<int>(i);
                }
            }

            uint8_t oldIdx = static_cast<uint8_t>(minIdx);
            uint8_t newIdx = static_cast<uint8_t>(bestMerge);
            for (auto& [sa, idx] : groupMap) {
                if (idx == oldIdx) idx = newIdx;
                else if (idx > oldIdx) --idx;
            }
            usage[bestMerge] += usage[minIdx];
            groupList.erase(groupList.begin() + minIdx);
            usage.erase(usage.begin() + minIdx);
        }
        MDX_LOG(_T("  → Merged down to %d groups\n"), (int)groupList.size());
    }

    Result result;
    result.vertexGroups.resize(assignments.size());
    for (size_t vi = 0; vi < assignments.size(); ++vi)
        result.vertexGroups[vi] = groupMap[assignments[vi]];

    for (auto& grp : groupList) {
        result.matrixGroups.push_back(static_cast<uint32_t>(grp.count));
        for (int i = 0; i < grp.count; ++i)
            result.matrixIndices.push_back(grp.boneIds[i]);
    }

    // Debug: dump first 10 matrix groups
    MDX_LOG(_T("  Matrix groups (first 10):\n"));
    uint32_t idxOffset = 0;
    for (size_t g = 0; g < std::min(result.matrixGroups.size(), size_t(10)); g++) {
        MDX_LOG(_T("    group[%d] size=%u bones:"), (int)g, result.matrixGroups[g]);
        for (uint32_t b = 0; b < result.matrixGroups[g]; b++) {
            if (idxOffset + b < result.matrixIndices.size())
                MDX_LOG(_T(" %u"), result.matrixIndices[idxOffset + b]);
        }
        MDX_LOG(_T("\n"));
        idxOffset += result.matrixGroups[g];
    }

    return result;
}

// ─────────────────────────────────────────────────────────────
MdxSkinQuantizer::Result
MdxSkinQuantizer::quantize(const ir::Mesh& mesh,
                            const MdxHierarchyResolver& hierarchy) {
    MDX_LOG(_T("── SkinQuantizer::quantize \"%S\" ──\n"), mesh.name.c_str());

    auto weights = extractWeights(mesh, hierarchy);
    auto adjacency = buildAdjacency(mesh.indices, mesh.vertices.size());
    relax(weights, mesh.vertices, adjacency);

    std::vector<SlotAssignment> assignments(weights.size());
    for (size_t vi = 0; vi < weights.size(); ++vi)
        assignments[vi] = assignSlots(weights[vi]);

    auto result = buildResult(assignments);

    MDX_LOG(_T("  Result: %d vertexGroups, %d matrixGroups, %d matrixIndices\n"),
            (int)result.vertexGroups.size(), (int)result.matrixGroups.size(),
            (int)result.matrixIndices.size());
    MDX_LOG(_T("── SkinQuantizer done ──\n\n"));

    return result;
}
