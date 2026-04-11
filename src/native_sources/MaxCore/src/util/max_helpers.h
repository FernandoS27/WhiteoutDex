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

// Read MDX node behavior flags from a Max node's user properties and
// inheritance flags.  Returns a bitmask matching MDX Node::NodeFlag layout:
//   0x01 DontInheritTranslation, 0x02 DontInheritRotation,
//   0x04 DontInheritScaling,     0x08 Billboarded,
//   0x10 BillboardedLockX,       0x20 BillboardedLockY,
//   0x40 BillboardedLockZ,       0x80 CameraAnchored
inline uint32_t collectNodeFlags(INode* node) {
    uint32_t flags = 0;
    Control* tmCtrl = node->GetTMController();
    if (tmCtrl) {
        DWORD inherit = tmCtrl->GetInheritanceFlags();
        if (!(inherit & INHERIT_POS_X) || !(inherit & INHERIT_POS_Y) || !(inherit & INHERIT_POS_Z))
            flags |= 0x1;
        if (!(inherit & INHERIT_ROT_X) || !(inherit & INHERIT_ROT_Y) || !(inherit & INHERIT_ROT_Z))
            flags |= 0x2;
        if (!(inherit & INHERIT_SCL_X) || !(inherit & INHERIT_SCL_Y) || !(inherit & INHERIT_SCL_Z))
            flags |= 0x4;
    }
    int val = 0;
    if (node->GetUserPropInt(_T("Billboarded"), val) && val)      flags |= 0x8;
    if (node->GetUserPropInt(_T("BillboardedLockX"), val) && val) flags |= 0x10;
    if (node->GetUserPropInt(_T("BillboardedLockY"), val) && val) flags |= 0x20;
    if (node->GetUserPropInt(_T("BillboardedLockZ"), val) && val) flags |= 0x40;
    if (node->GetUserPropInt(_T("CameraAnchored"), val) && val)   flags |= 0x80;
    return flags;
}

} // namespace core
