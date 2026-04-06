// MaxCore — ClassID constants for built-in Max types
#pragma once

#include <maxtypes.h>
#include <maxapi.h>
#include <CS/BIPEXP.H>

// BIPDRIVEN_CONTROL_CLASS_ID was added in Max 2020+ SDK.
// In older SDKs it's called BIPSLAVE_CONTROL_CLASS_ID (same value: 0x9154, 0).
#ifndef BIPDRIVEN_CONTROL_CLASS_ID
#define BIPDRIVEN_CONTROL_CLASS_ID  BIPSLAVE_CONTROL_CLASS_ID
#endif

namespace core_ids {
    // Standard Max bones
    inline const Class_ID BONE_OBJ_ID = BONE_OBJ_CLASSID;

    // CAT (runtime detection via interface preferred, but ClassIDs useful for quick checks)
    inline const Class_ID CAT_PARENT_ID = Class_ID(0x56AE72E5, 0x389B6659);
    inline const Class_ID CAT_BONE_ID   = Class_ID(0x2E6A0807, 0x15A72B1E);
    inline const Class_ID HUB_ID        = Class_ID(0x73DC4833, 0x65B45AEA);

    // Helpers
    inline const Class_ID DUMMY_ID       = Class_ID(DUMMY_CLASS_ID, 0);
    inline const Class_ID POINT_HELPER_ID = Class_ID(0x02013, 0x00000000);
    inline const Class_ID EXPOSETM_ID    = Class_ID(0x46972869, 0x2F7F0E3E);

    // Skin modifier
    inline const Class_ID SKIN_CLASS_ID  = Class_ID(9815843, 87654);

    // Edit Normals
    inline const Class_ID EDIT_NORMALS_CLASS_ID = Class_ID(0x4aa52ae3, 0x35ca1cde);
}
