// MDLXImporter — MDX ↔ Max coordinate transform (inverse of exporter)
//
// Exporter:  Max(x,y,z) → MDX(-y, x, z)   |   Max quat(x,y,z,w) → MDX(-y, x, z, w)
// Importer:  MDX(x,y,z) → Max(y, -x, z)   |   MDX quat(x,y,z,w) → Max(y, -x, z, w)
// Scale:     Exporter Max(x,y,z) → MDX(y, x, z) → Importer MDX(x,y,z) → Max(y, x, z)
#pragma once

#include <max.h>
#include <whiteout/vector_types.h>

namespace mdx_coord {

inline Point3 position(const whiteout::Vector3f& p) {
    return Point3(p.y, -p.x, p.z);
}

inline Point3 normal(const whiteout::Vector3f& n) {
    return Point3(n.y, -n.x, n.z);
}

inline Quat rotation(const whiteout::Quaternion& q) {
    return Quat(q.y, -q.x, q.z, q.w);
}

inline Point3 scale(const whiteout::Vector3f& s) {
    return Point3(s.y, s.x, s.z);
}

inline Point3 position(float x, float y, float z) {
    return Point3(y, -x, z);
}

inline Quat rotation(float x, float y, float z, float w) {
    return Quat(y, -x, z, w);
}

// Time conversion: MDX milliseconds → Max ticks (4800 ticks/sec)
inline TimeValue msToTicks(uint32_t ms) {
    return static_cast<TimeValue>(static_cast<int64_t>(ms) * 4800 / 1000);
}

} // namespace mdx_coord
