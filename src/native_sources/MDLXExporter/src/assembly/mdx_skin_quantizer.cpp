// MDLXExporter — v800 skin weight quantizer implementation
//
// Converts continuous [0,1] bone weights into the discrete bone-slot
// representation required by WC3 v800 matrix groups, where each slot
// contributes 1/N of the total weight (N = group size).
//
// Algorithm:
//   1. Extract & normalize per-vertex float weights (bone objectId → weight).
//   2. Iterative relaxation (Jacobi-style):
//      a. Distance-weighted averaging with spatial neighbors.
//      b. Softmax-like sharpening with annealing temperature (warm → cold).
//      c. Prune negligible weights and re-normalize.
//   3. Largest-remainder slot assignment: try all group sizes 1..4 and
//      pick the one that minimises squared error vs. original weights.
//   4. Collect unique slot combinations into matrix groups (max 255).

#include "mdx_skin_quantizer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_set>

// ─────────────────────────────────────────────────────────────
// SlotAssignment comparison (sorted bone list)
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
// Weight helpers
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
    // Numerically stable softmax: subtract max before exp
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
// Stage 1: extract weights from IR mesh
// ─────────────────────────────────────────────────────────────

std::vector<MdxSkinQuantizer::VertexWeights>
MdxSkinQuantizer::extractWeights(const ir::Mesh& mesh,
                                  const MdxHierarchyResolver& hierarchy) const {
    using wdx = whiteout::mdx::Node;
    std::vector<VertexWeights> out(mesh.vertices.size());

    for (size_t vi = 0; vi < mesh.vertices.size(); ++vi) {
        auto& vert = mesh.vertices[vi];
        auto& vw = out[vi];

        for (auto& inf : vert.skinInfluences) {
            if (inf.boneIndex < 0 || inf.weight <= 0.0f) continue;
            uint32_t objId = hierarchy.getObjectId(inf.boneIndex);
            if (objId == wdx::NO_PARENT) continue;
            mergeWeight(vw, objId, inf.weight);
        }

        // Sort descending by weight for deterministic processing
        std::sort(vw.begin(), vw.end(),
                  [](const BoneWeight& a, const BoneWeight& b) {
                      return a.weight > b.weight;
                  });
        normalize(vw);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────
// Adjacency graph from triangle indices
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

    // Convert to vector-of-vectors
    std::vector<std::vector<uint32_t>> result(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i)
        result[i].assign(adj[i].begin(), adj[i].end());
    return result;
}

// ─────────────────────────────────────────────────────────────
// Stage 2: iterative relaxation (Jacobi-style)
// ─────────────────────────────────────────────────────────────

void MdxSkinQuantizer::relax(
    std::vector<VertexWeights>& weights,
    const std::vector<ir::Vertex>& vertices,
    const std::vector<std::vector<uint32_t>>& adjacency) const
{
    const size_t N = weights.size();
    if (N == 0) return;

    std::vector<VertexWeights> next(N);

    for (int iter = 0; iter < kRelaxIterations; ++iter) {
        // Anneal: temperature goes from kTempStart (soft) → kTempEnd (sharp)
        float t = (kRelaxIterations > 1)
                      ? static_cast<float>(iter) / (kRelaxIterations - 1)
                      : 1.0f;
        float temperature = kTempStart * (1.0f - t) + kTempEnd * t;

        for (size_t vi = 0; vi < N; ++vi) {
            // Start from the vertex's own weights, scaled by (1 - alpha)
            VertexWeights blended;
            for (auto& bw : weights[vi])
                mergeWeight(blended, bw.boneId, bw.weight * (1.0f - kRelaxAlpha));

            // Accumulate distance-weighted neighbor contributions
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

            // Normalise the neighbor portion so it sums to kRelaxAlpha
            if (totalInvDist > 1e-7f && !neighbors.empty()) {
                // The own-weight portion already sums to ~(1 - alpha).
                // The neighbor portion sums to totalInvDist * ~1.0.
                // We need to scale the neighbor portion so the total
                // neighbor contribution is alpha.
                float neighborSum = 0.0f;
                float ownSum = 0.0f;
                for (auto& bw : blended) neighborSum += bw.weight;
                for (auto& bw : weights[vi]) ownSum += bw.weight;
                ownSum *= (1.0f - kRelaxAlpha);
                float rawNeighborSum = neighborSum - ownSum;
                if (rawNeighborSum > 1e-7f) {
                    float scale = kRelaxAlpha / rawNeighborSum;
                    // Re-build: own contribution is already correct, scale the rest
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

            // Keep bones sorted descending
            std::sort(blended.begin(), blended.end(),
                      [](const BoneWeight& a, const BoneWeight& b) {
                          return a.weight > b.weight;
                      });

            // Clamp to 4 influences max
            if (blended.size() > kMaxSlots)
                blended.resize(kMaxSlots);
            normalize(blended);

            next[vi] = std::move(blended);
        }

        weights.swap(next);
    }
}

// ─────────────────────────────────────────────────────────────
// Stage 3: largest-remainder slot assignment
// ─────────────────────────────────────────────────────────────

MdxSkinQuantizer::SlotAssignment
MdxSkinQuantizer::assignSlots(const VertexWeights& weights) const {
    // Fallback: single bone group 0
    if (weights.empty()) {
        SlotAssignment sa;
        sa.boneIds[0] = 0;
        sa.count = 1;
        return sa;
    }

    // v800 matrix groups contain unique bone IDs only.
    // Each bone in the group contributes 1/N to the vertex transform.
    // Take the top bones by weight (weights are sorted descending).
    int numBones = static_cast<int>(std::min(weights.size(), size_t(kMaxSlots)));

    SlotAssignment sa;
    sa.count = numBones;
    for (int i = 0; i < numBones; ++i)
        sa.boneIds[i] = weights[i].boneId;

    // Sort bone IDs for canonical ordering (required for group deduplication)
    std::sort(sa.boneIds, sa.boneIds + sa.count);
    return sa;
}

// ─────────────────────────────────────────────────────────────
// Stage 4: collect unique groups, build final result
// ─────────────────────────────────────────────────────────────

MdxSkinQuantizer::Result
MdxSkinQuantizer::buildResult(const std::vector<SlotAssignment>& assignments) const {
    // Map unique slot assignments → group index
    std::map<SlotAssignment, uint8_t> groupMap;
    std::vector<SlotAssignment> groupList;

    for (auto& sa : assignments) {
        if (groupMap.find(sa) == groupMap.end()) {
            uint8_t idx = static_cast<uint8_t>(groupList.size());
            groupMap[sa] = idx;
            groupList.push_back(sa);
        }
    }

    // If we exceed 255 unique groups, merge the least-used ones into
    // their nearest neighbour by bone overlap.
    if (groupList.size() > kMaxGroups) {
        // Count usage of each group
        std::vector<int> usage(groupList.size(), 0);
        for (auto& sa : assignments)
            usage[groupMap[sa]]++;

        while (groupList.size() > kMaxGroups) {
            // Find the least-used group
            int minIdx = 0;
            for (size_t i = 1; i < groupList.size(); ++i) {
                if (usage[i] < usage[minIdx]) minIdx = static_cast<int>(i);
            }

            // Find the most similar other group (max shared bones)
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

            // Remap: all vertices in minIdx → bestMerge
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
    }

    // Build output arrays
    Result result;
    result.vertexGroups.resize(assignments.size());
    for (size_t vi = 0; vi < assignments.size(); ++vi)
        result.vertexGroups[vi] = groupMap[assignments[vi]];

    for (auto& grp : groupList) {
        result.matrixGroups.push_back(static_cast<uint32_t>(grp.count));
        for (int i = 0; i < grp.count; ++i)
            result.matrixIndices.push_back(grp.boneIds[i]);
    }

    return result;
}

// ─────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────

MdxSkinQuantizer::Result
MdxSkinQuantizer::quantize(const ir::Mesh& mesh,
                            const MdxHierarchyResolver& hierarchy) {
    // 1. Extract per-vertex weights (bone objectId → float weight)
    auto weights = extractWeights(mesh, hierarchy);

    // 2. Build adjacency graph from mesh triangles
    auto adjacency = buildAdjacency(mesh.indices, mesh.vertices.size());

    // 3. Iterative relaxation: spatial smoothing + annealing sharpening
    relax(weights, mesh.vertices, adjacency);

    // 4. Assign each vertex to optimal bone slots via largest-remainder
    std::vector<SlotAssignment> assignments(weights.size());
    for (size_t vi = 0; vi < weights.size(); ++vi)
        assignments[vi] = assignSlots(weights[vi]);

    // 5. Collect unique groups and build final arrays
    return buildResult(assignments);
}
