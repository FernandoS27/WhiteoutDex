// MDLXExporter — v800 skin weight quantizer implementation
//
// A v800 matrix group is a *set* of bones whose transforms the runtime
// averages with equal weight. A group of k bones can therefore represent
// exactly one weight vector: (1/k, …, 1/k). Converting a float-weighted skin
// into matrix groups is then nearest-neighbour quantization — pick the k whose
// uniform vector sits closest to the vertex's actual weights.
//
// The previous implementation ran iterative neighbour relaxation plus a
// softmax "sharpen" pass before assigning slots. Both are modelling
// operations, not format conversion: they rewrote weights the user never
// authored. On an already-quantized classic model (imported weights are exactly
// 1/n per matrix group) this was destructive — round-tripping
// saurus_warrior_unit_run moved 120/622 body vertices to different bone sets,
// 8 of them to a *disjoint* set, and collapsed the sword's
// Arm2+Hand blend down to Arm2 alone. Result: torn geometry.
//
// The rule below is exactly idempotent on already-quantized input, because a
// uniform vector always quantizes back to itself.
#include "mdx_skin_quantizer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

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

        normalize(vw);
        // Drop bleed weights before the snap. Even the smallest legal uniform
        // share is 1/kMaxSlots, well above kMinWeight, so this can never
        // discard a bone that an already-quantized model actually used.
        prune(vw, kMinWeight);
        normalize(vw);

        // Descending weight — assignSlots relies on this ordering.
        std::sort(vw.begin(), vw.end(),
                  [](const BoneWeight& a, const BoneWeight& b) {
                      if (a.weight != b.weight) return a.weight > b.weight;
                      return a.boneId < b.boneId;   // stable tie-break
                  });
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
// Nearest-uniform snap.
//
// Keeping the top k bones costs, in squared error against the normalized
// weight vector w (sorted descending, sum 1):
//
//   E(k) = Σ_{i<k} (w_i − 1/k)²  +  Σ_{i≥k} w_i²
//        = Σ_i w_i²  +  (1 − 2·S_k) / k          where S_k = Σ_{i<k} w_i
//
// The Σ w_i² term is constant across k, so minimizing E(k) is minimizing
// (1 − 2·S_k)/k. Ties resolve to the smaller k (fewer bones, smaller MATS).
//
// Sanity checks this reproduces: {1.0} → 1 bone; {0.5,0.5} → 2 bones;
// {0.25×4} → 4 bones (all exact round-trips of classic data); {0.8,0.2} → 1
// bone and {0.7,0.3} → 2 bones (crossover at 0.75, the midpoint between the
// 1.0 and 0.5 lattice points); {0.97,0.03} → 1 bone.
MdxSkinQuantizer::SlotAssignment
MdxSkinQuantizer::assignSlots(const VertexWeights& weights) const {
    if (weights.empty()) {
        SlotAssignment sa;
        sa.boneIds[0] = 0;
        sa.count = 1;
        return sa;
    }

    const int maxK = static_cast<int>(std::min(weights.size(), size_t(kMaxSlots)));

    int   bestK    = 1;
    float bestCost = std::numeric_limits<float>::max();
    float prefix   = 0.0f;

    for (int k = 1; k <= maxK; ++k) {
        prefix += weights[k - 1].weight;
        float cost = (1.0f - 2.0f * prefix) / static_cast<float>(k);
        if (cost < bestCost - 1e-6f) {   // strict: ties keep the smaller k
            bestCost = cost;
            bestK = k;
        }
    }

    SlotAssignment sa;
    sa.count = bestK;
    for (int i = 0; i < bestK; ++i)
        sa.boneIds[i] = weights[i].boneId;

    std::sort(sa.boneIds, sa.boneIds + sa.count);
    return sa;
}

// ─────────────────────────────────────────────────────────────
MdxSkinQuantizer::Result
MdxSkinQuantizer::buildResult(const std::vector<SlotAssignment>& assignments) const {
    // Unique groups, in first-use order, with a usage count each.
    std::map<SlotAssignment, uint32_t> groupMap;   // group -> slot in groupList
    std::vector<SlotAssignment> groupList;
    std::vector<uint32_t>       usage;

    for (auto& sa : assignments) {
        auto [it, inserted] =
            groupMap.emplace(sa, static_cast<uint32_t>(groupList.size()));
        if (inserted) {
            groupList.push_back(sa);
            usage.push_back(0);
        }
        usage[it->second]++;
    }

    MDX_LOG(_T("  buildResult: %d unique matrix groups from %d vertices\n"),
            (int)groupList.size(), (int)assignments.size());

    // GNDX is u8, so at most 255 groups survive. Fold the least-used groups
    // into their closest surviving neighbour (largest bone overlap, then
    // smallest symmetric difference, then highest usage).
    //
    // `resolved` maps every original slot to the slot that now represents it;
    // the compaction at the end derives the final u8 indices from it. The old
    // code decremented indices in place while remapping, which shifted the
    // merge *target* out from under the vertices that had just been pointed at
    // it whenever the target sat above the victim — sending those vertices to
    // an unrelated bone set.
    std::vector<uint32_t> resolved(groupList.size());
    std::iota(resolved.begin(), resolved.end(), 0u);
    std::vector<bool> alive(groupList.size(), true);
    size_t aliveCount = groupList.size();

    if (aliveCount > kMaxGroups) {
        MDX_LOG(_T("  ⚠ Exceeds %d groups! Merging...\n"), kMaxGroups);

        while (aliveCount > kMaxGroups) {
            // Least-used surviving group is the victim.
            int victim = -1;
            for (size_t i = 0; i < groupList.size(); ++i) {
                if (!alive[i]) continue;
                if (victim < 0 || usage[i] < usage[victim]) victim = static_cast<int>(i);
            }
            if (victim < 0) break;

            const SlotAssignment& vg = groupList[victim];

            int      target     = -1;
            int      bestScore  = std::numeric_limits<int>::min();
            uint32_t bestUsage  = 0;
            for (size_t i = 0; i < groupList.size(); ++i) {
                if (!alive[i] || static_cast<int>(i) == victim) continue;
                const SlotAssignment& tg = groupList[i];

                int overlap = 0;
                for (int a = 0; a < vg.count; ++a)
                    for (int b = 0; b < tg.count; ++b)
                        if (vg.boneIds[a] == tg.boneIds[b]) { ++overlap; break; }

                // 2*overlap - (|A| + |B|) == -(symmetric difference size).
                int score = 2 * overlap - (vg.count + tg.count);
                if (score > bestScore ||
                    (score == bestScore && usage[i] > bestUsage)) {
                    bestScore = score;
                    bestUsage = usage[i];
                    target = static_cast<int>(i);
                }
            }
            if (target < 0) break;

            for (auto& r : resolved)
                if (r == static_cast<uint32_t>(victim)) r = static_cast<uint32_t>(target);
            usage[target] += usage[victim];
            alive[victim] = false;
            --aliveCount;
        }
        MDX_LOG(_T("  → Merged down to %d groups\n"), (int)aliveCount);
    }

    // Compact surviving slots to consecutive u8 indices.
    std::vector<uint8_t> compact(groupList.size(), 0);
    Result result;
    {
        uint32_t next = 0;
        for (size_t i = 0; i < groupList.size(); ++i) {
            if (!alive[i]) continue;
            compact[i] = static_cast<uint8_t>(next++);
            result.matrixGroups.push_back(static_cast<uint32_t>(groupList[i].count));
            for (int b = 0; b < groupList[i].count; ++b)
                result.matrixIndices.push_back(groupList[i].boneIds[b]);
        }
    }

    result.vertexGroups.resize(assignments.size());
    for (size_t vi = 0; vi < assignments.size(); ++vi)
        result.vertexGroups[vi] = compact[resolved[groupMap[assignments[vi]]]];

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
