// MaxCore — Common Max SDK utility functions
#pragma once

#include <max.h>
#include <inode.h>
#include <triobj.h>
#include <modstack.h>
#include <maxtypes.h>

namespace core {

// Quaternion dot product (not provided by Max SDK 2026)
inline float quatDot(const Quat& a, const Quat& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
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

} // namespace core
