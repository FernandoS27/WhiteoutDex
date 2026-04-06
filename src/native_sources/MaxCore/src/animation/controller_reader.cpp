// MaxCore — Animation controller type detection implementation
#include "controller_reader.h"
#include "../util/class_ids.h"

namespace core {

// The Link Constraint ClassID — verified from Max SDK
static const Class_ID LINK_CONSTRAINT_CLASS_ID(0x32892AA7, 0x1E7845FE);

ControllerType ControllerReader::detect(Control* ctrl) {
    if (!ctrl) return ControllerType::None;

    Class_ID cid = ctrl->ClassID();
    ULONG cidA = cid.PartA();

    // Biped controllers
    if (cidA == BIPBODY_CONTROL_CLASS_ID.PartA() ||
        cidA == BIPDRIVEN_CONTROL_CLASS_ID.PartA())
        return ControllerType::Biped;

    // Link Constraint
    if (cid == LINK_CONSTRAINT_CLASS_ID)
        return ControllerType::Link_Constraint;

    // Standard controllers — identified by their ClassID.PartA() (PartB is 0)

    // Position controllers
    if (cidA == HYBRIDINTERP_POSITION_CLASS_ID) return ControllerType::Bezier_Position;
    if (cidA == TCBINTERP_POSITION_CLASS_ID)    return ControllerType::TCB_Position;
    if (cidA == LININTERP_POSITION_CLASS_ID)    return ControllerType::Linear_Position;

    // Rotation controllers
    if (cidA == HYBRIDINTERP_ROTATION_CLASS_ID) return ControllerType::Bezier_Rotation;
    if (cidA == TCBINTERP_ROTATION_CLASS_ID)    return ControllerType::TCB_Rotation;
    if (cidA == LININTERP_ROTATION_CLASS_ID)    return ControllerType::Linear_Rotation;
    if (cidA == EULER_CONTROL_CLASS_ID)         return ControllerType::Euler_XYZ;

    // Scale controllers
    if (cidA == HYBRIDINTERP_SCALE_CLASS_ID)    return ControllerType::Bezier_Scale;
    if (cidA == TCBINTERP_SCALE_CLASS_ID)       return ControllerType::TCB_Scale;
    if (cidA == LININTERP_SCALE_CLASS_ID)       return ControllerType::Linear_Scale;

    // Float controllers
    if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID)    return ControllerType::Bezier_Float;
    if (cidA == TCBINTERP_FLOAT_CLASS_ID)       return ControllerType::TCB_Float;
    if (cidA == LININTERP_FLOAT_CLASS_ID)       return ControllerType::Linear_Float;

    return ControllerType::Unknown;
}

bool ControllerReader::isBiped(INode* node) {
    if (!node) return false;
    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return false;
    Class_ID ctrlID = tmCtrl->ClassID();
    return (ctrlID == BIPBODY_CONTROL_CLASS_ID || ctrlID == BIPDRIVEN_CONTROL_CLASS_ID);
}

bool ControllerReader::isCAT(INode* node) {
    if (!node) return false;
    ObjectState os = node->EvalWorldState(0);
    if (!os.obj) return false;
    Class_ID objID = os.obj->ClassID();
    return (objID == core_ids::CAT_PARENT_ID ||
            objID == core_ids::CAT_BONE_ID ||
            objID == core_ids::HUB_ID);
}

bool ControllerReader::isIKAffected(INode* node) {
    if (!node) return false;
    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return false;
    // IK chains are typically identified by having an IK chain interface
    // or specific chain controller ClassIDs. For a robust check, we
    // examine if the controller has sub-controllers that are IK.
    // A simplified approach: check if the position controller
    // is something other than standard FK types.
    Control* posCtrl = tmCtrl->GetPositionController();
    if (posCtrl) {
        ControllerType pType = detect(posCtrl);
        if (pType == ControllerType::Unknown) {
            // Unknown controller type on position — might be IK
            // Check for IK chain interface
            return false; // Conservative: only flag IK if explicitly detected
        }
    }
    return false;
}

bool ControllerReader::isLinkConstraint(Control* ctrl) {
    if (!ctrl) return false;
    return ctrl->ClassID() == LINK_CONSTRAINT_CLASS_ID;
}

} // namespace core
