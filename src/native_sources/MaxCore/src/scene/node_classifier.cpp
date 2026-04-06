// MaxCore — Node classification implementation
#include "node_classifier.h"
#include "../util/class_ids.h"

#include <object.h>
#include <triobj.h>
#include <genlight.h>
#include <gencam.h>
#include <CS/BIPEXP.H>

namespace core {

void NodeClassifier::registerClassID(Class_ID id, const std::string& tag) {
    customClassIDs_[packClassID(id)] = tag;
}

NodeCategory NodeClassifier::classify(INode* node, std::string& outTag) const {
    if (!node) return NodeCategory::Ignored;

    outTag.clear();
    ObjectState os = node->EvalWorldState(0);
    Object* obj = os.obj;
    if (!obj) return NodeCategory::Ignored;

    Class_ID objClassID = obj->ClassID();
    SClass_ID superClassID = obj->SuperClassID();

    // 1. Check custom ClassIDs first (format-specific nodes)
    auto it = customClassIDs_.find(packClassID(objClassID));
    if (it != customClassIDs_.end()) {
        outTag = it->second;
        return NodeCategory::Custom;
    }

    // 2. Bone detection: BoneGeometry
    if (objClassID == BONE_OBJ_CLASSID)
        return NodeCategory::Bone;

    // 3. Biped detection via TM controller ClassID
    Control* tmCtrl = node->GetTMController();
    if (tmCtrl) {
        Class_ID ctrlID = tmCtrl->ClassID();
        if (ctrlID == BIPBODY_CONTROL_CLASS_ID ||
            ctrlID == BIPDRIVEN_CONTROL_CLASS_ID)
            return NodeCategory::Bone;
    }
    // Also detect biped geometry objects
    if (objClassID == SKELOBJ_CLASS_ID)
        return NodeCategory::Bone;

    // 4. CAT bones
    if (objClassID == core_ids::CAT_PARENT_ID ||
        objClassID == core_ids::CAT_BONE_ID ||
        objClassID == core_ids::HUB_ID)
        return NodeCategory::Bone;

    // 5. Cameras
    if (superClassID == CAMERA_CLASS_ID)
        return NodeCategory::Camera;

    // 6. Standard lights
    if (superClassID == LIGHT_CLASS_ID)
        return NodeCategory::StandardLight;

    // 7. Helpers (Dummy, Point, ExposeTM)
    if (superClassID == HELPER_CLASS_ID) {
        if (objClassID == core_ids::DUMMY_ID ||
            objClassID == core_ids::POINT_HELPER_ID ||
            objClassID == core_ids::EXPOSETM_ID)
            return NodeCategory::Helper;
        // Generic helper
        return NodeCategory::Helper;
    }

    // 8. Geometry — check if convertible to TriObject
    if (superClassID == GEOMOBJECT_CLASS_ID) {
        if (obj->CanConvertToType(triObjectClassID))
            return NodeCategory::Mesh;
    }

    return NodeCategory::Ignored;
}

} // namespace core
