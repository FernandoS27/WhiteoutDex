// MaxCore — MassFX baked animation detector
//
// MassFX bakes physics simulation into standard controllers (Position_XYZ,
// Euler_XYZ) inside Position_List / Rotation_List controllers. The baked
// sub-controllers are named "MassFX Baked Position" / "MassFX Baked Rotation".
//
// Detection strategy:
//   1. Check if the TM controller's position or rotation sub-controller
//      is a List controller (Position_List ClassID = {0x4B4B1003, 0} or
//      Rotation_List ClassID = {0x4B4B1004, 0}).
//   2. Iterate the List's sub-anims checking if any name contains "MassFX".
//
// Since GetNodeTM() already evaluates List controllers correctly (composing
// all sub-controllers with their weights), MassFX nodes need no special
// sampling — just the same GetNodeTM resampling path used for Biped/CAT/Link.
#pragma once

#include <max.h>
#include <inode.h>
#include <control.h>

namespace core {

class MassFXDetector {
public:
    // ClassIDs for List controllers (from scan: position_list=1263210498,0  rotation_list=1263210499,0)
    static inline const Class_ID POSITION_LIST_CLASS_ID = Class_ID(1263210498u, 0);
    static inline const Class_ID ROTATION_LIST_CLASS_ID = Class_ID(1263210499u, 0);

    /// Returns true if the node has MassFX baked animation on its
    /// position or rotation controller (i.e. a List controller containing
    /// a sub-anim whose name includes "MassFX").
    static bool isMassFXBaked(INode* node);

    /// Check a specific controller for MassFX list entries.
    /// Works for both position and rotation List controllers.
    static bool hasMassFXSubController(Control* ctrl);

private:
    /// Check if an Animatable's sub-anim tree contains a "MassFX" named entry.
    static bool hasNamedMassFXSubAnim(Animatable* anim);
};

} // namespace core
