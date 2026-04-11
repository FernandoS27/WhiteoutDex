// MaxCore — MassFX baked animation detector implementation
//
// MassFX bakes physics into Position_List / Rotation_List controllers.
// Sub-anims are named "MassFX Baked Position" / "MassFX Baked Rotation"
// and contain standard Position_XYZ / Euler_XYZ controllers with bezier_float
// keys on each axis.
//
// Detection is name-based (checking SubAnimName for "MassFX") because the
// baked controllers are standard Max types — there is no unique MassFX ClassID.
// The name check is robust across Max versions since the bake function always
// uses these specific names.
#include "massfx_detector.h"

#include <cstring>
#include <cwchar>

namespace core {

// Case-insensitive wcsstr — checks if |haystack| contains |needle|.
static bool wcsContainsI(const MCHAR* haystack, const wchar_t* needle) {
    if (!haystack || !needle) return false;
    size_t hLen = wcslen(haystack);
    size_t nLen = wcslen(needle);
    if (nLen > hLen) return false;
    for (size_t i = 0; i <= hLen - nLen; ++i) {
        bool match = true;
        for (size_t j = 0; j < nLen; ++j) {
            if (towlower(haystack[i + j]) != towlower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool MassFXDetector::hasNamedMassFXSubAnim(Animatable* anim) {
    if (!anim) return false;
    int numSubs = anim->NumSubs();
    for (int i = 0; i < numSubs; ++i) {
        // Get the sub-anim name — this is the label shown in Track View
        // (e.g. "MassFX Baked Position", "MassFX Baked Rotation")
        // Max 2022+ requires a bool localized parameter; we use false
        // to get the English name for reliable detection.
        MSTR name = anim->SubAnimName(i, false);
        if (wcsContainsI(name.data(), L"MassFX"))
            return true;
    }
    return false;
}

bool MassFXDetector::hasMassFXSubController(Control* ctrl) {
    if (!ctrl) return false;

    Class_ID cid = ctrl->ClassID();

    // Only check List controllers
    if (cid != POSITION_LIST_CLASS_ID && cid != ROTATION_LIST_CLASS_ID)
        return false;

    return hasNamedMassFXSubAnim(ctrl);
}

bool MassFXDetector::isMassFXBaked(INode* node) {
    if (!node) return false;

    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return false;

    // Check position controller
    Control* posCtrl = tmCtrl->GetPositionController();
    if (posCtrl && hasMassFXSubController(posCtrl))
        return true;

    // Check rotation controller
    Control* rotCtrl = tmCtrl->GetRotationController();
    if (rotCtrl && hasMassFXSubController(rotCtrl))
        return true;

    return false;
}

} // namespace core
