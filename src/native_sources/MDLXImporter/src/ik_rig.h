// MDLXImporter - IK Rig
//
// Puts an IK rig on the arms and legs of an imported model, the way a
// hand-built Max rig looks (Star master, Circle per hand, Rectangle per foot,
// Ellipse swivel target per elbow/knee), moves every animation onto it and
// links weapons that are held beside a hand with a Link Constraint.
//
// HOW IT STAYS EXACT
//   Max's IK solvers decide the roll of the upper and lower bone from the plane
//   of shoulder, elbow and hand alone; a bone's own rotation keys are ignored
//   while the solver runs (measured in Max 2026). Animations made with an IK
//   rig keep that roll constant, most imported ones do not (Biped arms roll up
//   to 177 degrees). So the solver drives a hidden chain instead:
//     * a driver chain (3 bones, IK Limb solver) follows the controls;
//     * each real bone turns by rotation_list [IK, Offset, Twist]:
//         IK      Orientation_Constraint to its driver bone (hand/foot: to its control)
//         Offset  constant: the turn between driver and bone in the setup frame
//         Twist   keys: what the solver cannot reproduce (roll, overstretched
//                 elbows) - near zero for animations made with IK
//       Max combines the layers as  local = Twist * Offset * IK.
//   Position and scale of the bones stay untouched. Every frame is compared
//   with the original transforms; keys are added until each one matches.
//
// The objects are created through short MaxScript calls (a handful per limb,
// milliseconds); everything that runs per frame is here in C++.
// The window (src/post_startup_scripts/IKRigCreator.ms) and the tests call the
// same functions through the MaxScript primitives in ik_rig_mxs.cpp.
#pragma once

#include <max.h>
#include <string>
#include <vector>

namespace wdx {
namespace ikrig {

struct Limb {
    std::wstring label;       // "L Arm", "R Leg", ...
    INode* upper = nullptr;   // shoulder / hip joint
    INode* lower = nullptr;   // elbow / knee
    INode* end = nullptr;     // hand / foot
    bool isLeg = false;
    std::wstring how;         // what found it: an attachment point, a name, the lowest bone
};

struct Result {
    std::vector<std::wstring> lines;   // the report, one line per limb / weapon / note
    int limbs = 0;                     // limbs rigged (or bones keyed again on removal)
    int weapons = 0;
    bool cancelled = false;
};

// Arms and legs of the scene. Notes on ends that have no usable chain go to `notes`.
std::vector<Limb> detectLimbs(std::vector<std::wstring>* notes);

// Builds the rig on `which` (nullptr: the detected limbs). Does nothing when the
// scene has a rig already.
Result build(const std::vector<Limb>* which, bool showProgress);

// Takes the rig off again: the bones and weapons get plain keys (exact on every
// frame), the rig objects and layers go.
Result remove(bool showProgress);

bool hasRig();

// Adds the time of each stage to the report lines (tests only).
void setDebugTiming(bool on);

} // namespace ikrig
} // namespace wdx
