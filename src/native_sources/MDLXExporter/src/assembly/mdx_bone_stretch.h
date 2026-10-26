// MDLXExporter — bone stretch as two extra nodes
//
// A Max bone whose length or scale changes stretches: Max keeps that in the
// bone's stretch TM, which Skin applies (objectTM = objectOffsetTM *
// stretchTM * nodeTM; INode::GetStretchTM "if a plug-in (such as skin) wants
// the stretchTM included"), but which never reaches the node TM. So nothing
// the exporter bakes carries it, and the bone's children do not inherit it.
// MDX has no stretch. The DBLImport Sparking Zero rigs scale SPINE3 up to
// 65x this way; the game showed the chest where Max had it at frame 0.
//
// For the skin the stretch is a scale along the bone's own axes, about its
// pivot, that only the bone's own vertices feel. Relative to frame 0 (the
// pose the vertices are exported in) a vertex of the bone moves by
//   inverse(St(0) * Node(0)) * St(t) * Node(t)
//     = T(-p0) * [Q0^-1 * St(0)^-1 * St(t) * Q0] * [Q0^-1 * Node(t)],
// Q0 the bone's frame-0 orientation (its frame-0 scale, being diagonal too,
// commutes out). The last factor is the bone as it already exports, the
// bracket the stretch. MDX scales before it rotates, so two nodes at the
// bone's pivot give the bracket:
//   "<bone> StretchAxis" (helper, child of the bone): KGRT Q0, KGSC the stretch
//   "<bone> Stretch"     (bone, child of the helper):  KGRT Q0^-1
// and the bone's skin weights move to "<bone> Stretch". The bone itself and
// its children are left as they were.
#pragma once

#include <core/intermediate_types.h>

#include <max.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mdx_assembly {

namespace stretch_detail {

// The orientation of a TM: its rows at unit length, one turned round when
// the TM mirrors (the dispatcher's extractRotation does the same).
inline Matrix3 orientation(const Matrix3& tm) {
    Point3 r0 = Normalize(tm.GetRow(0));
    Point3 r1 = Normalize(tm.GetRow(1));
    Point3 r2 = Normalize(tm.GetRow(2));
    if (DotProd(r0, CrossProd(r1, r2)) < 0.0f)
        r0 = -r0;
    return Matrix3(r0, r1, r2, Point3(0, 0, 0));
}

inline Quat toQuat(const Matrix3& rotation) {
    Quat q(rotation);
    q.Normalize();
    return q;
}

// The stretch at `t` relative to `base`, along the bone's axes. Signed: a
// child that passes behind the bone turns the stretch negative (SPINE3 of
// Broly_SZ_Super reaches -18.5), which a negative KGSC reproduces.
inline Point3 relativeStretch(INode* node, const Matrix3& baseInverse, TimeValue t) {
    Matrix3 st = node->GetStretchTM(t);
    st.NoTrans();
    const Matrix3 rel = baseInverse * st;
    return Point3(rel.GetRow(0).x, rel.GetRow(1).y, rel.GetRow(2).z);
}

inline std::vector<TimeValue> frameTimes(TimeValue start, TimeValue end, int step) {
    std::vector<TimeValue> times;
    for (TimeValue t = start; t <= end; t += step)
        times.push_back(t);
    if (times.empty() || times.back() != end)
        times.push_back(end);
    return times;
}

} // namespace stretch_detail

struct StretchedBone {
    std::string name;
    INode* node = nullptr;
    float largest = 1.0f;   // the stretch factor furthest from 1
};

// Adds the two nodes for every skin bone that stretches in a sequence and
// moves its weights to the new bone. Call after the animation bake.
inline std::vector<StretchedBone> addBoneStretchNodes(ir::IRModel& ir, int tickInterval) {
    using namespace stretch_detail;
    std::vector<StretchedBone> done;
    if (ir.sequences.empty())
        return done;
    if (tickInterval <= 0)
        tickInterval = GetTicksPerFrame();

    // Nodes that carry skin weights.
    std::unordered_set<int32_t> weighted;
    for (const auto& mesh : ir.meshes)
        for (const auto& v : mesh.vertices)
            for (const auto& inf : v.skinInfluences)
                if (inf.boneIndex >= 0 && inf.weight > 0.0f)
                    weighted.insert(inf.boneIndex);

    std::unordered_map<int32_t, int32_t> remap;   // stretched node -> its "Stretch" node
    const size_t nodeCount = ir.nodes.size();
    for (size_t ni = 0; ni < nodeCount; ++ni) {
        if (!weighted.count(static_cast<int32_t>(ni)))
            continue;
        INode* node = ir.nodes[ni].maxNode;
        if (!node)
            continue;
        const int32_t boneIdx = [&] {
            for (size_t b = 0; b < ir.bones.size(); ++b)
                if (ir.bones[b].nodeIndex == static_cast<int32_t>(ni))
                    return static_cast<int32_t>(b);
            return -1;
        }();
        if (boneIdx < 0)
            continue;

        Matrix3 base = node->GetStretchTM(0);
        base.NoTrans();
        if (std::fabs(DotProd(base.GetRow(0), CrossProd(base.GetRow(1), base.GetRow(2)))) < 1e-12f)
            continue;
        const Matrix3 baseInverse = Inverse(base);

        // The stretch per frame of every sequence; skipped when it never
        // leaves 1.
        std::vector<std::vector<std::pair<TimeValue, Point3>>> perSequence;
        float largest = 1.0f;
        for (const auto& seq : ir.sequences) {
            std::vector<std::pair<TimeValue, Point3>> samples;
            for (TimeValue t : frameTimes(seq.startTime, seq.endTime, tickInterval)) {
                const Point3 s = relativeStretch(node, baseInverse, t);
                for (int a = 0; a < 3; ++a)
                    if (std::fabs(s[a] - 1.0f) > std::fabs(largest - 1.0f))
                        largest = s[a];
                samples.emplace_back(t, s);
            }
            perSequence.push_back(std::move(samples));
        }
        if (std::fabs(largest - 1.0f) < 1e-3f)
            continue;

        // Copies: ir.nodes grows below.
        const std::string boneName = ir.nodes[ni].name;
        const Point3 pivot = ir.nodes[ni].pivotPoint;
        const Matrix3 worldTM = ir.nodes[ni].worldTM;
        const Matrix3 bindPose = ir.bones[boneIdx].bindPose;
        const Matrix3 q0 = orientation(node->GetNodeTM(0));
        const Quat axisRot = toQuat(q0);
        const Quat backRot = toQuat(Inverse(q0));

        auto addNode = [&](const std::string& name, int32_t parentNode, int32_t parentBone,
                           bool helper) {
            ir::IRModel::Node n;
            n.name = name;
            n.parentIndex = parentNode;
            n.pivotPoint = pivot;
            n.worldTM = worldTM;
            const int32_t nodeIdx = static_cast<int32_t>(ir.nodes.size());
            ir.nodes.push_back(std::move(n));
            ir::Bone b;
            b.name = name;
            b.nodeIndex = nodeIdx;
            b.parentIndex = parentBone;
            b.pivotPoint = pivot;
            b.bindPose = bindPose;
            b.isHelper = helper;
            ir.bones.push_back(std::move(b));
            return nodeIdx;
        };
        const int32_t axisNode = addNode(boneName + " StretchAxis", static_cast<int32_t>(ni),
                                         boneIdx, true);
        const int32_t axisBone = static_cast<int32_t>(ir.bones.size()) - 1;
        const int32_t stretchNode = addNode(boneName + " Stretch", axisNode, axisBone, false);

        for (size_t si = 0; si < ir.sequences.size(); ++si) {
            const auto& seq = ir.sequences[si];
            auto constantRot = [&](const Quat& q) {
                ir::QuatTrack track;
                track.interpolation = ir::InterpolationType::Linear;
                ir::Keyframe<Quat> k;
                k.value = q;
                k.time = seq.startTime;
                track.keys.push_back(k);
                if (seq.endTime != seq.startTime) {
                    k.time = seq.endTime;
                    track.keys.push_back(k);
                }
                return track;
            };
            ir::NodeAnimation axis;
            axis.nodeIndex = axisNode;
            axis.rotation = constantRot(axisRot);
            axis.scale.interpolation = ir::InterpolationType::Linear;
            for (const auto& [t, s] : perSequence[si]) {
                ir::Keyframe<Point3> k;
                k.time = t;
                k.value = s;
                axis.scale.keys.push_back(k);
            }
            ir.nodeAnimations.push_back(std::move(axis));

            ir::NodeAnimation back;
            back.nodeIndex = stretchNode;
            back.rotation = constantRot(backRot);
            ir.nodeAnimations.push_back(std::move(back));
        }

        remap[static_cast<int32_t>(ni)] = stretchNode;
        done.push_back({boneName, node, largest});
    }

    if (!remap.empty())
        for (auto& mesh : ir.meshes)
            for (auto& v : mesh.vertices)
                for (auto& inf : v.skinInfluences) {
                    auto it = remap.find(inf.boneIndex);
                    if (it != remap.end())
                        inf.boneIndex = it->second;
                }
    return done;
}

} // namespace mdx_assembly
