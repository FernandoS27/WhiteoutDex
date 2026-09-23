// MaxCore — Common Max SDK utility functions
#pragma once

#include <max.h>
#include <inode.h>
#include <triobj.h>
#include <modstack.h>
#include <maxtypes.h>

#include <algorithm>
#include <cmath>

namespace core {

// Quaternion dot product (not provided by Max SDK 2026)
inline float quatDot(const Quat& a, const Quat& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

// Angle in radians between two rotations, as 4·asin(|a−b|/2) rather than
// 2·acos(dot): for near-identical rotations dot rounds to exactly 1.0 in
// float32, which reads any motion under ~0.03° as zero. |a+b| covers q ≡ −q.
inline float quatAngle(const Quat& a, const Quat& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z, dw = a.w - b.w;
    float sx = a.x + b.x, sy = a.y + b.y, sz = a.z + b.z, sw = a.w + b.w;
    float distSq = std::min(dx*dx + dy*dy + dz*dz + dw*dw,
                            sx*sx + sy*sy + sz*sz + sw*sw);
    float halfDist = std::sqrt(distSq) * 0.5f;
    if (halfDist >= 1.0f) return 3.14159265f;
    return 4.0f * std::asin(halfDist);
}

// Evaluate a node to a TriObject; set needDelete=true if caller must free
TriObject* getTriObject(INode* node, TimeValue t, bool& needDelete);

// Compute local TM relative to parent
Matrix3 getLocalTM(INode* node, TimeValue t);

// Get the scene root node
INode* getMaxRoot();

// Check if a node has a Skin modifier
bool hasSkinModifier(INode* node);

// Walk the modifier stack to find a modifier by ClassID
Modifier* findModifierByClassID(INode* node, Class_ID id);

// Read MDX node behavior flags from a Max node's user properties.
// Returns a bitmask matching MDX Node::NodeFlag layout:
//   0x01 DontInheritTranslation, 0x02 DontInheritRotation,
//   0x04 DontInheritScaling,     0x08 Billboarded,
//   0x10 BillboardedLockX,       0x20 BillboardedLockY,
//   0x40 BillboardedLockZ,       0x80 CameraAnchored
inline uint32_t collectNodeFlags(INode* node) {
    uint32_t flags = 0;
    int val = 0;
    // DontInherit — read from UserProperties (written by importer/ObjectSettings).
    // Note: GetInheritanceFlags() returns 0 for some controller types (e.g. after
    // import), which falsely triggers all DontInherit flags. UserProperties are
    // reliable since the importer and ObjectSettings dialog write them explicitly.
    if (node->GetUserPropInt(_T("DontInheritTranslation"), val) && val) flags |= 0x1;
    val = 0;
    if (node->GetUserPropInt(_T("DontInheritRotation"), val) && val)    flags |= 0x2;
    val = 0;
    if (node->GetUserPropInt(_T("DontInheritScaling"), val) && val)     flags |= 0x4;
    val = 0;
    if (node->GetUserPropInt(_T("Billboarded"), val) && val)            flags |= 0x8;
    val = 0;
    if (node->GetUserPropInt(_T("BillboardedLockX"), val) && val)       flags |= 0x10;
    val = 0;
    if (node->GetUserPropInt(_T("BillboardedLockY"), val) && val)       flags |= 0x20;
    val = 0;
    if (node->GetUserPropInt(_T("BillboardedLockZ"), val) && val)       flags |= 0x40;
    val = 0;
    if (node->GetUserPropInt(_T("CameraAnchored"), val) && val)        flags |= 0x80;
    return flags;
}

} // namespace core
