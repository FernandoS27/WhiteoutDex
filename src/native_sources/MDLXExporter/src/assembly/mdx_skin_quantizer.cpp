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
#include <array>
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
// Sampled over the sequences in proportion to their length, at most one sample
// per frame. The bind pose is frame 0 (AnimDispatcher, MeshExtractor), so a
// vertex follows a bone by v * Inverse(TM(0)) * TM(t).
MdxBoneMotion::MdxBoneMotion(const ir::IRModel& ir) : ir_(ir) {
    constexpr int kSamples = 160;

    TimeValue total = 0;
    for (const auto& seq : ir.sequences)
        if (seq.endTime > seq.startTime) total += seq.endTime - seq.startTime;
    if (total <= 0) return;

    const TimeValue tpf = std::max(1, GetTicksPerFrame());
    for (const auto& seq : ir.sequences) {
        const TimeValue len = seq.endTime - seq.startTime;
        if (len <= 0) continue;
        int n = static_cast<int>(std::lround(double(kSamples) * len / total));
        n = std::clamp(n, 2, std::max(2, len / tpf + 1));
        for (int i = 0; i < n; ++i)
            times_.push_back(seq.startTime + static_cast<TimeValue>(len * (i + 0.5) / n));
    }
}

const std::vector<Matrix3>& MdxBoneMotion::movesOf(int32_t irNode) {
    auto found = moves_.find(irNode);
    if (found != moves_.end()) return found->second;

    auto& out = moves_[irNode];
    // A stretch helper has no scene node of its own; it rides on its parent.
    INode* node = nullptr;
    for (int32_t n = irNode; !node && n >= 0 && n < static_cast<int32_t>(ir_.nodes.size());
         n = ir_.nodes[n].parentIndex)
        node = ir_.nodes[n].maxNode;
    if (node) {
        const Matrix3 fromBind = Inverse(node->GetNodeTM(0));
        out.reserve(times_.size());
        for (TimeValue t : times_)
            out.push_back(fromBind * node->GetNodeTM(t));
    }
    return out;
}

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
void MdxSkinQuantizer::mergeWeight(VertexWeights& w, uint32_t boneId, float weight, int32_t irNode) {
    for (auto& bw : w) {
        if (bw.boneId == boneId) { bw.weight += weight; return; }
    }
    w.push_back({boneId, weight, irNode});
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
            mergeWeight(vw, objId, inf.weight, inf.boneIndex);
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
// Gradient mode: the group of at most four entries over the vertex's four
// strongest bones that lands closest to where Max's Skin puts the vertex.
//
// A group gives the vertex the weights w + dw. Because dw sums to zero, the
// vertex's own position cancels and its distance from the true position at one
// moment is |Σ dw_i · d_i|, d_i being how far bone i alone would have carried it
// since the bind pose. The squared distance over all moments is dw' G dw with G
// the matrix of the dot products of the d_i, so G is built once per vertex and
// the candidates only cost a small sum each.
//
// Bones that move as one do not tell the candidates apart, and without
// animation nothing does; the small pull towards the authored weights decides
// those. Weights that already are a group's ratios (an import of such a model)
// cost nothing and come back unchanged.
MdxSkinQuantizer::SlotAssignment
MdxSkinQuantizer::assignGradient(const VertexWeights& weights, const Point3& position,
                                 MdxBoneMotion& motion) const {
    if (weights.size() <= 1) return assignSlots(weights);

    constexpr double kWeightPull = 0.01;   // model units², for a full weight of error
    const size_t k = weights.size();

    std::vector<const std::vector<Matrix3>*> moves(k);
    for (size_t i = 0; i < k; ++i)
        moves[i] = &motion.movesOf(weights[i].irNode);

    std::vector<double> gram(k * k, 0.0);
    std::vector<Point3> carried(k);
    const size_t samples = motion.sampleCount();
    for (size_t s = 0; s < samples; ++s) {
        for (size_t i = 0; i < k; ++i) {
            carried[i] = moves[i]->empty() ? Point3(0.f, 0.f, 0.f)
                                           : position * (*moves[i])[s] - position;
            // A bone scaled to nothing at frame 0 has no inverse.
            if (!std::isfinite(carried[i].x) || !std::isfinite(carried[i].y) ||
                !std::isfinite(carried[i].z))
                carried[i] = Point3(0.f, 0.f, 0.f);
        }
        for (size_t i = 0; i < k; ++i)
            for (size_t j = i; j < k; ++j) {
                const double dot = double(carried[i].x) * carried[j].x +
                                   double(carried[i].y) * carried[j].y +
                                   double(carried[i].z) * carried[j].z;
                gram[i * k + j] += dot;
                if (i != j) gram[j * k + i] += dot;
            }
    }
    if (samples > 1)
        for (double& g : gram) g /= static_cast<double>(samples);

    // Shortest first, so a tie keeps the smaller group: [A,B], not [A,A,B,B].
    static const std::vector<std::array<int, 4>> kCandidates = [] {
        std::vector<std::array<int, 4>> t;
        for (int n = 1; n <= kGradientEntries; ++n)
            for (int a = n; a >= 0; --a)
                for (int b = n - a; b >= 0; --b)
                    for (int c = n - a - b; c >= 0; --c)
                        t.push_back({a, b, c, n - a - b - c});
        return t;
    }();

    std::vector<double> dw(k);
    const std::array<int, 4>* best = nullptr;
    double bestCost = std::numeric_limits<double>::max();
    for (const auto& cand : kCandidates) {
        int n = 0;
        bool usable = true;
        for (size_t i = 0; i < cand.size(); ++i) {
            n += cand[i];
            if (cand[i] > 0 && i >= k) usable = false;
        }
        if (!usable) continue;

        for (size_t i = 0; i < k; ++i)
            dw[i] = (i < cand.size() ? double(cand[i]) / n : 0.0) - weights[i].weight;
        double cost = 0.0;
        for (size_t i = 0; i < k; ++i) {
            cost += kWeightPull * dw[i] * dw[i];
            for (size_t j = 0; j < k; ++j)
                cost += dw[i] * dw[j] * gram[i * k + j];
        }
        if (cost < bestCost - 1e-12) {
            bestCost = cost;
            best = &cand;
        }
    }
    if (!best) return assignSlots(weights);

    SlotAssignment sa;
    for (size_t i = 0; i < best->size(); ++i)
        for (int c = 0; c < (*best)[i]; ++c)
            sa.boneIds[sa.count++] = weights[i].boneId;
    std::sort(sa.boneIds, sa.boneIds + sa.count);
    return sa;
}

// Squared distance of the weights two groups stand for. Both lists are sorted.
double MdxSkinQuantizer::ratioDistance(const SlotAssignment& a, const SlotAssignment& b) {
    double dist = 0.0;
    int i = 0, j = 0;
    while (i < a.count || j < b.count) {
        const uint32_t id = (j >= b.count || (i < a.count && a.boneIds[i] <= b.boneIds[j]))
                                ? a.boneIds[i] : b.boneIds[j];
        int ca = 0, cb = 0;
        while (i < a.count && a.boneIds[i] == id) { ++ca; ++i; }
        while (j < b.count && b.boneIds[j] == id) { ++cb; ++j; }
        const double d = double(ca) / a.count - double(cb) / b.count;
        dist += d * d;
    }
    return dist;
}

// ─────────────────────────────────────────────────────────────
MdxSkinQuantizer::Result
MdxSkinQuantizer::buildResult(const std::vector<SlotAssignment>& assignments, bool byRatio) const {
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

    if (aliveCount > kMaxGroups && byRatio) {
        // Repeated bones make many more groups, and two groups with the same
        // bones can stand for quite different weights. So what goes first is
        // the group whose vertices lose least (how many, times how far) moving
        // to the group nearest in weights.
        std::vector<int>    nearest(groupList.size(), -1);
        std::vector<double> nearestDist(groupList.size(), 0.0);
        auto findNearest = [&](size_t g) {
            nearest[g] = -1;
            nearestDist[g] = std::numeric_limits<double>::max();
            for (size_t o = 0; o < groupList.size(); ++o) {
                if (!alive[o] || o == g) continue;
                const double d = ratioDistance(groupList[g], groupList[o]);
                if (d < nearestDist[g]) { nearestDist[g] = d; nearest[g] = static_cast<int>(o); }
            }
        };
        for (size_t g = 0; g < groupList.size(); ++g) findNearest(g);

        while (aliveCount > kMaxGroups) {
            int victim = -1;
            double least = std::numeric_limits<double>::max();
            for (size_t g = 0; g < groupList.size(); ++g) {
                if (!alive[g] || nearest[g] < 0) continue;
                const double loss = usage[g] * nearestDist[g];
                if (loss < least) { least = loss; victim = static_cast<int>(g); }
            }
            if (victim < 0) break;

            const int target = nearest[victim];
            for (auto& r : resolved)
                if (r == static_cast<uint32_t>(victim)) r = static_cast<uint32_t>(target);
            usage[target] += usage[victim];
            alive[victim] = false;
            --aliveCount;
            for (size_t g = 0; g < groupList.size(); ++g)
                if (alive[g] && nearest[g] == victim) findNearest(g);
        }
        MDX_LOG(_T("  -> Merged down to %d groups by ratio\n"), (int)aliveCount);
    } else if (aliveCount > kMaxGroups) {
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
                            const MdxHierarchyResolver& hierarchy,
                            MdxBoneMotion* motion) {
    MDX_LOG(_T("── SkinQuantizer::quantize \"%S\" ──\n"), mesh.name.c_str());

    auto weights = extractWeights(mesh, hierarchy);

    std::vector<SlotAssignment> assignments(weights.size());
    for (size_t vi = 0; vi < weights.size(); ++vi)
        assignments[vi] = motion ? assignGradient(weights[vi], mesh.vertices[vi].position, *motion)
                                 : assignSlots(weights[vi]);

    auto result = buildResult(assignments, motion != nullptr);

    MDX_LOG(_T("  Result: %d vertexGroups, %d matrixGroups, %d matrixIndices\n"),
            (int)result.vertexGroups.size(), (int)result.matrixGroups.size(),
            (int)result.matrixIndices.size());
    MDX_LOG(_T("── SkinQuantizer done ──\n\n"));

    return result;
}
