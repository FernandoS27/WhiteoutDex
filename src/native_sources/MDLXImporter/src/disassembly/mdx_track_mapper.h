// MDLXImporter — Map mdx::Track<T> → ir::Track<T>
#pragma once

#include <core/intermediate_types.h>
#include <whiteout/models/mdx/structures.h>
#include "mdx_coord_transform.h"

namespace mdx_disasm {

// ── Interpolation type mapping ──────────────────────────────

inline ir::InterpolationType mapInterpolation(whiteout::mdx::InterpolationType t) {
    switch (t) {
    case whiteout::mdx::InterpolationType::None:    return ir::InterpolationType::None;
    case whiteout::mdx::InterpolationType::Linear:  return ir::InterpolationType::Linear;
    case whiteout::mdx::InterpolationType::Hermite: return ir::InterpolationType::Hermite;
    case whiteout::mdx::InterpolationType::Bezier:  return ir::InterpolationType::Bezier;
    default:                                        return ir::InterpolationType::Linear;
    }
}

// ── Generic track mapper ────────────────────────────────────
// Track storage is SoA: timestamps[] holds frame numbers; keys() / tangentKeys()
// expose value-only spans. We zip them index-wise to rebuild AoS Keyframes.

template <typename SrcT, typename DstT, typename Convert>
ir::Track<DstT> mapTrack(const whiteout::mdx::Track<SrcT>& src, Convert convert)
{
    ir::Track<DstT> dst;
    dst.interpolation = mapInterpolation(src.interpolationType);
    dst.globalSequenceIndex = (src.globalSequenceId == whiteout::mdx::Track<SrcT>::kNoGlobalSequence)
        ? -1 : static_cast<int32_t>(src.globalSequenceId);

    if (whiteout::mdx::isSmoothInterpolation(src.interpolationType)) {
        auto keys = src.tangentKeys();
        dst.keys.reserve(keys.size());
        for (size_t i = 0; i < keys.size(); ++i) {
            ir::Keyframe<DstT> kf;
            kf.time = mdx_coord::msToTicks(src.timestamps[i]);
            kf.value = convert(keys[i].value);
            kf.inTangent = convert(keys[i].inTan);
            kf.outTangent = convert(keys[i].outTan);
            kf.hasTangents = true;
            dst.keys.push_back(kf);
        }
    } else {
        auto keys = src.keys();
        dst.keys.reserve(keys.size());
        for (size_t i = 0; i < keys.size(); ++i) {
            ir::Keyframe<DstT> kf;
            kf.time = mdx_coord::msToTicks(src.timestamps[i]);
            kf.value = convert(keys[i]);
            dst.keys.push_back(kf);
        }
    }
    return dst;
}

// ── Specialized mappers with coordinate transform ───────────

inline ir::Vec3Track mapPositionTrack(const whiteout::mdx::Track<whiteout::Vector3f>& src)
{
    return mapTrack<whiteout::Vector3f, Point3>(
        src, [](const whiteout::Vector3f& v) { return Point3(v.x, v.y, v.z); });
}

inline ir::QuatTrack mapRotationTrack(const whiteout::mdx::Track<whiteout::Quaternion>& src)
{
    return mapTrack<whiteout::Quaternion, Quat>(
        src, [](const whiteout::Quaternion& q) { return Quat(q.x, q.y, q.z, q.w); });
}

inline ir::Vec3Track mapScaleTrack(const whiteout::mdx::Track<whiteout::Vector3f>& src)
{
    return mapTrack<whiteout::Vector3f, Point3>(
        src, [](const whiteout::Vector3f& v) { return Point3(v.x, v.y, v.z); });
}

inline ir::FloatTrack mapFloatTrack(const whiteout::mdx::Track<float>& src)
{
    return mapTrack<float, float>(
        src, [](const float& v) { return v; });
}

inline ir::IntTrack mapIntTrack(const whiteout::mdx::Track<uint32_t>& src)
{
    ir::Track<int32_t> dst;
    dst.interpolation = mapInterpolation(src.interpolationType);
    dst.globalSequenceIndex = (src.globalSequenceId == whiteout::mdx::Track<uint32_t>::kNoGlobalSequence)
        ? -1 : static_cast<int32_t>(src.globalSequenceId);
    auto keys = src.keys();
    dst.keys.reserve(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
        ir::Keyframe<int32_t> kf;
        kf.time = mdx_coord::msToTicks(src.timestamps[i]);
        kf.value = static_cast<int32_t>(keys[i]);
        dst.keys.push_back(kf);
    }
    return dst;
}

// ── Color track mapper ──────────────────────────────────────

inline ir::ColorTrack mapColorTrack(const whiteout::mdx::Track<whiteout::Vector3f>& src)
{
    return mapTrack<whiteout::Vector3f, Color>(
        src,
        [](const whiteout::Vector3f& v) { return Color(v.x, v.y, v.z); });
}

// ── Vec4 (RGBA) track mapper ────────────────────────────────

inline ir::Vec4Track mapVec4Track(const whiteout::mdx::Track<whiteout::Vector4f>& src)
{
    return mapTrack<whiteout::Vector4f, Point4>(
        src,
        [](const whiteout::Vector4f& v) { return Point4(v.x, v.y, v.z, v.w); });
}

} // namespace mdx_disasm
