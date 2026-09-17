// MaxCore — Camera extractor implementation
#include "camera_extractor.h"

#include <gencam.h>
#include <object.h>

namespace core {

ir::Camera CameraExtractor::extract(INode* node, TimeValue t, ExportErrorReporter& reporter) {
    ir::Camera cam;

    const MCHAR* name = node->GetName();
    if (name) {
        std::wstring wname(name);
        cam.name.assign(wname.begin(), wname.end());
    }

    // Position from the camera node's TM
    Matrix3 nodeTM = node->GetNodeTM(t);
    cam.position = nodeTM.GetTrans();

    // Get the camera object
    ObjectState os = node->EvalWorldState(t);
    Object* obj = os.obj;
    if (!obj || obj->SuperClassID() != CAMERA_CLASS_ID) {
        reporter.warning(L"Node '" + std::wstring(name ? name : L"<unnamed>") +
                         L"' is not a camera object.");
        return cam;
    }

    GenCamera* genCam = dynamic_cast<GenCamera*>(obj);
    if (genCam) {
        // FOV
        Interval iv = FOREVER;
        cam.fov = genCam->GetFOV(t, iv);

        // Clip planes
        cam.nearClip = genCam->GetClipDist(t, CAM_HITHER_CLIP);
        cam.farClip = genCam->GetClipDist(t, CAM_YON_CLIP);
    } else {
        // Cameras from other plug-ins (the Physical Camera, say) still
        // answer the CameraObject evaluation every renderer uses.
        CameraState state;
        Interval iv = FOREVER;
        if (static_cast<CameraObject*>(obj)->EvalCameraState(t, iv, &state) == REF_SUCCEED) {
            cam.fov = state.fov;
            cam.nearClip = state.hither;
            cam.farClip = state.yon;
        }
    }

    // Target camera: get the target node's position
    if (!genCam || genCam->IsOrtho() == FALSE) {
        INode* target = node->GetTarget();
        if (target) {
            Matrix3 targetTM = target->GetNodeTM(t);
            cam.targetPosition = targetTM.GetTrans();
        }
    }

    return cam;
}

} // namespace core
