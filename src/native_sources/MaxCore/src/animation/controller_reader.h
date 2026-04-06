// MaxCore — Animation controller type detection
#pragma once

#include <max.h>
#include <inode.h>
#include <control.h>
#include <plugapi.h>
#include <CS/BIPEXP.H>

namespace core {

enum class ControllerType {
    Bezier_Position, TCB_Position, Linear_Position, Position_XYZ,
    Bezier_Rotation, TCB_Rotation, Linear_Rotation, Euler_XYZ,
    Bezier_Scale, TCB_Scale, Linear_Scale, ScaleXYZ,
    Bezier_Float, TCB_Float, Linear_Float,
    Biped, CAT,
    Link_Constraint,
    Unknown, None
};

class ControllerReader {
public:
    static ControllerType detect(Control* ctrl);
    static bool isBiped(INode* node);
    static bool isCAT(INode* node);
    static bool isIKAffected(INode* node);
    static bool isLinkConstraint(Control* ctrl);
};

} // namespace core
