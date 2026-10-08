// MDLXImporter — Map mdx::Track<T> → ir::Track<T>
#pragma once

#include <type_traits>

#include <core/intermediate_types.h>
#include <whiteout/models/mdx/structures.h>
#include "mdx_coord_transform.h"

#include <cmath>
#include <vector>

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

// ── Keys that snap onto one frame ───────────────────────────
// msToTicks rounds key times to whole frames, so keys closer together than a
// frame land on the same tick, and Max holds one key per tick. Keep the key
// whose own time is nearest that frame (on a tie the later one). Keeping the
// last one instead let a key just past a sequence end replace the sequence's
// own closing key: PandarenBrewmaster's head spins in the gap after
// Stand - 1 (keys at 4835..4841 ms), and 4841 ms took over frame 145, the
// loop's last key at 4833 ms, so the head snapped around on every loop.
template <typename T, typename Ms>
void collapseSnappedKeys(std::vector<ir::Keyframe<T>>& keys, const std::vector<Ms>& ms)
{
    auto offFrame = [](Ms t, TimeValue tick) {
        // MDX times are signed (see msToTicks), WhiteoutLib stores them as u32
        const double ms = std::is_unsigned_v<Ms> && sizeof(Ms) == 4
            ? static_cast<double>(static_cast<int32_t>(t)) : static_cast<double>(t);
        return std::abs(ms * 4.8 - static_cast<double>(tick));
    };
    size_t out = 0;
    std::vector<Ms> outMs;
    outMs.reserve(ms.size());
    for (size_t i = 0; i < keys.size(); ++i) {
        if (out > 0 && keys[out - 1].time == keys[i].time) {
            if (offFrame(ms[i], keys[i].time) <= offFrame(outMs.back(), keys[i].time)) {
                keys[out - 1] = keys[i];
                outMs.back() = ms[i];
            }
            continue;
        }
        keys[out++] = keys[i];
        outMs.push_back(ms[i]);
    }
    keys.resize(out);
}

// ── Global sequence tracks ──────────────────────────────────
// The game plays a global-sequence track at (time mod duration): keys past
// the duration are never reached, and a duration of 0 shows the value at
// time 0 for good. The exporter reads a global sequence's duration from its
// controller's last key, so every such track ends with a key exactly at the
// duration; a 0 duration becomes a constant one-frame global sequence (two
// equal keys), which plays the same - without it a lone key or the keys the
// file carried past 0 turned a still track into a loop (DNC Felwood terrain,
// Dalaran sky, Illidan imprisoned).
// The disassembler sets the model's durations (ms) before mapping tracks.
inline const std::vector<uint32_t>*& currentGlobalSequences()
{
    static const std::vector<uint32_t>* durations = nullptr;
    return durations;
}

template <typename T>
void fitToGlobalSequence(ir::Track<T>& track)
{
    const auto* durations = currentGlobalSequences();
    if (!durations || track.globalSequenceIndex < 0 || track.keys.empty()) return;
    if (static_cast<size_t>(track.globalSequenceIndex) >= durations->size()) return;
    auto& keys = track.keys;
    const TimeValue d = mdx_coord::msToTicks((*durations)[track.globalSequenceIndex]);
    if (d <= 0) {
        size_t pick = 0;
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i].time <= 0) pick = i;
        ir::Keyframe<T> k = keys[pick];
        k.time = 0;
        k.hasTangents = false;
        keys.assign(1, k);
        k.time = GetTicksPerFrame();
        keys.push_back(k);
        return;
    }
    if (keys.back().time == d) return;
    size_t keep = 0;   // keys at or before the duration
    while (keep < keys.size() && keys[keep].time <= d) ++keep;
    ir::Keyframe<T> end = keys[keep > 0 ? keep - 1 : 0];
    keys.resize(keep > 0 ? keep : 0);
    if (!keys.empty() && keys.back().time == d) return;
    end.time = d;
    end.hasTangents = false;
    keys.push_back(end);
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
    collapseSnappedKeys(dst.keys, src.timestamps);
    fitToGlobalSequence(dst);
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
    collapseSnappedKeys(dst.keys, src.timestamps);
    fitToGlobalSequence(dst);
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
