// MDLXExporter — Coordinate transform: Max space → MDX space
#pragma once

#include <whiteout/models/mdx/types.h>
#include <whiteout/vector_types.h>
#include <max.h>
#include <cstdint>

namespace mdx_transform {

// Max: X-right, Y-forward, Z-up  →  MDX: X(-Y), Y(X), Z(Z)
inline whiteout::Vector3f position(const Point3& p) {
    return {-p.y, p.x, p.z};
}

inline whiteout::Vector3f normal(const Point3& n) {
    return {-n.y, n.x, n.z};
}

inline whiteout::Quaternion rotation(const Quat& q) {
    // Max Quat: (x, y, z, w) → MDX Quat: (-y, x, z, w) — same axis remap as position
    return {-q.y, q.x, q.z, q.w};
}

inline whiteout::Vector3f scale(const Point3& s) {
    // Scale axes swap to match position axis permutation: Max(x,y,z) → MDX(y,x,z)
    return {s.y, s.x, s.z};
}

inline whiteout::Vector3f color3f(const Color& c) {
    return {c.r, c.g, c.b};
}

inline whiteout::Vector3f color3f(const Point3& c) {
    return {c.x, c.y, c.z};
}

inline whiteout::Vector2f texcoord(const Point2& uv) {
    return {uv.x, uv.y};
}

// Time: Max ticks (4800/sec) → MDX milliseconds
inline uint32_t ticksToMs(TimeValue t) {
    return static_cast<uint32_t>((static_cast<int64_t>(t) * 1000) / 4800);
}

// Convert a Max frame number to MDX milliseconds
// Max uses frameTicks = frame * GetTicksPerFrame() but sequences store frame numbers
// Standard: 30fps → ticksPerFrame=160 → frame * 160 ticks → ms
inline uint32_t frameToMs(int frame, int ticksPerFrame = 160) {
    return ticksToMs(static_cast<TimeValue>(frame * ticksPerFrame));
}

} // namespace mdx_transform
