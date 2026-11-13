// MDLXImporter - MaxScript access to the IK Rig (ik_rig.h)
//
//   wdxIKRigDetect()              -> #(#(label, upper, lower, end, isLeg, how), ...)
//   wdxIKRigBuild limbs:<array>   -> report lines (array of strings); limbs as from
//                                    wdxIKRigDetect, default: the detected ones
//   wdxIKRigRemove()              -> report lines
//   wdxIKRigHas()                 -> true when the scene has a rig
//
// The window (src/post_startup_scripts/IKRigCreator.ms) and the tests use these;
// the importer calls wdx::ikrig::build directly.

#include <max.h>
#include <maxscript/maxscript.h>
#include <maxscript/foundation/numbers.h>
#include <maxscript/foundation/strings.h>
#include <maxscript/foundation/arrays.h>
#include <maxscript/maxwrapper/mxsobjects.h>
#include <maxscript/foundation/name.h>
#include <maxscript/macros/define_instantiation_functions.h>

#include "ik_rig.h"

namespace {

Value* lines(const wdx::ikrig::Result& r)
{
    one_typed_value_local(Array* out);
    vl.out = new Array(static_cast<int>(r.lines.size()));
    for (const auto& s : r.lines) vl.out->append(new String(s.c_str()));
    return_value(vl.out);
}

} // namespace

def_visible_primitive(wdxIKRigDetect, "wdxIKRigDetect");
Value* wdxIKRigDetect_cf(Value** arg_list, int count)
{
    check_arg_count(wdxIKRigDetect, 0, count);
    const std::vector<wdx::ikrig::Limb> limbs = wdx::ikrig::detectLimbs(nullptr);
    one_typed_value_local(Array* out);
    vl.out = new Array(static_cast<int>(limbs.size()));
    for (const auto& l : limbs) {
        Array* a = new Array(6);
        a->append(new String(l.label.c_str()));
        a->append(MAXNode::intern(l.upper));
        a->append(MAXNode::intern(l.lower));
        a->append(MAXNode::intern(l.end));
        a->append(l.isLeg ? &true_value : &false_value);
        a->append(new String(l.how.c_str()));
        vl.out->append(a);
    }
    return_value(vl.out);
}

def_visible_primitive(wdxIKRigBuild, "wdxIKRigBuild");
Value* wdxIKRigBuild_cf(Value** arg_list, int count)
{
    // wdxIKRigBuild [limbs:<array>] [progress:<bool>]
    Value* limbsArg = _get_key_arg(arg_list, count, Name::intern(_M("limbs")));
    Value* progArg = _get_key_arg(arg_list, count, Name::intern(_M("progress")));
    const bool showProgress = progArg == &unsupplied || progArg->to_bool();
    Value* debugArg = _get_key_arg(arg_list, count, Name::intern(_M("debug")));
    wdx::ikrig::setDebugTiming(debugArg != &unsupplied && debugArg->to_bool());
    if (limbsArg == &unsupplied || limbsArg == &undefined)
        return lines(wdx::ikrig::build(nullptr, showProgress));
    std::vector<wdx::ikrig::Limb> which;
    Array* arr = static_cast<Array*>(limbsArg);
    if (!is_array(limbsArg)) throw RuntimeError(_M("wdxIKRigBuild: limbs: needs an array"));
    for (int i = 0; i < arr->size; ++i) {
        Value* item = arr->data[i];
        if (!is_array(item)) continue;
        Array* a = static_cast<Array*>(item);
        if (a->size < 5) continue;
        wdx::ikrig::Limb l;
        l.label = a->data[0]->to_string();
        l.upper = is_node(a->data[1]) ? a->data[1]->to_node() : nullptr;
        l.lower = is_node(a->data[2]) ? a->data[2]->to_node() : nullptr;
        l.end = is_node(a->data[3]) ? a->data[3]->to_node() : nullptr;
        l.isLeg = a->data[4]->to_bool() != FALSE;
        l.how = a->size > 5 ? a->data[5]->to_string() : L"";
        if (l.upper && l.lower && l.end) which.push_back(l);
    }
    return lines(wdx::ikrig::build(&which, showProgress));
}

def_visible_primitive(wdxIKRigRemove, "wdxIKRigRemove");
Value* wdxIKRigRemove_cf(Value** arg_list, int count)
{
    Value* progArg = _get_key_arg(arg_list, count, Name::intern(_M("progress")));
    const bool showProgress = progArg == &unsupplied || progArg->to_bool();
    return lines(wdx::ikrig::remove(showProgress));
}

def_visible_primitive(wdxIKRigHas, "wdxIKRigHas");
Value* wdxIKRigHas_cf(Value** arg_list, int count)
{
    check_arg_count(wdxIKRigHas, 0, count);
    return wdx::ikrig::hasRig() ? &true_value : &false_value;
}
