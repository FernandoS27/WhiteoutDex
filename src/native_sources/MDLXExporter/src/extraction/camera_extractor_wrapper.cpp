// MDLXExporter — Camera extractor implementation
//
// Finds all native Max cameras in the scene and extracts:
//   * Name, position, target position
//   * FOV, near/far clip
//   * Animation tracks (position / targetPosition / rotation)
//
// Based on the existing MaxCore CameraExtractor for static values;
// animation sampling is done here at the sequence-frame level.

#include "camera_extractor_wrapper.h"
#include <extraction/camera_extractor.h>   // core::CameraExtractor

#include <max.h>
#include <object.h>
#include <gencam.h>
#include <inode.h>
#include <control.h>
#include <istdplug.h>

#include <cstdio>
#include <cstdarg>

// Debug logging — same file as material extractor uses
static void cam_log(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    char tempPath[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, tempPath);
    if (n == 0 || n > MAX_PATH) return;

    char logPath[MAX_PATH];
    snprintf(logPath, sizeof(logPath), "%smdlx_material_debug.log", tempPath);

    FILE* f = nullptr;
    if (fopen_s(&f, logPath, "a") == 0 && f) {
        fputs(buf, f);
        fclose(f);
    }
}

namespace mdx_extract {

namespace {

// Check if a node holds a Max camera object.
bool isCameraNode(INode* node) {
    if (!node) return false;
    ObjectState os = node->EvalWorldState(0);
    if (!os.obj) return false;
    return os.obj->SuperClassID() == CAMERA_CLASS_ID;
}

// Sample a camera track: collect keyframes from the PRS controller.
// Returns -1 if no animation, else index into model.vec3Tracks.
int32_t extractPositionTrack(INode* node, ir::IRModel& model) {
    if (!node) return -1;
    Control* prs = node->GetTMController();
    if (!prs) return -1;
    Control* posCtrl = prs->GetPositionController();
    if (!posCtrl) return -1;

    IKeyControl* ikc = GetKeyControlInterface(posCtrl);
    if (!ikc) return -1;
    int n = ikc->GetNumKeys();
    if (n < 2) return -1;  // static position → no track

    ir::Track<Point3> track;
    track.interpolation = ir::InterpolationType::Linear;

    for (int i = 0; i < n; i++) {
        IBezPoint3Key key;
        ikc->GetKey(i, &key);

        Point3 val(0, 0, 0);
        Interval iv = FOREVER;
        posCtrl->GetValue(key.time, &val, iv);

        track.keys.push_back({});
        auto& k = track.keys.back();
        k.time = key.time;
        k.value = val;
        k.inTangent = Point3(0, 0, 0);
        k.outTangent = Point3(0, 0, 0);
        k.hasTangents = false;
    }

    int32_t idx = static_cast<int32_t>(model.vec3Tracks.size());
    model.vec3Tracks.push_back(std::move(track));
    return idx;
}

// Extract target position animation (for target cameras).
int32_t extractTargetTrack(INode* target, ir::IRModel& model) {
    return extractPositionTrack(target, model);
}

} // anon namespace

void extractCameras(const std::vector<core::SceneNode>& nodes,
                    ir::IRModel& model,
                    core::ExportErrorReporter& reporter)
{
    cam_log("\n==== Camera Extractor ====\n");
    cam_log("Scanning %d nodes for cameras...\n", (int)nodes.size());

    core::CameraExtractor coreExtractor;
    int found = 0;

    for (auto& sn : nodes) {
        if (!sn.maxNode) continue;
        if (!isCameraNode(sn.maxNode)) continue;

        const MCHAR* nm = sn.maxNode->GetName();
        cam_log("  Found camera: '%ls'\n", nm ? nm : L"<unnamed>");

        // Extract static data via the MaxCore extractor
        ir::Camera cam = coreExtractor.extract(sn.maxNode, 0, reporter);

        // Extract animation tracks
        cam.positionTrackIndex = extractPositionTrack(sn.maxNode, model);
        cam_log("    positionTrackIdx=%d\n", cam.positionTrackIndex);

        INode* target = sn.maxNode->GetTarget();
        if (target) {
            cam.targetPositionTrackIndex = extractTargetTrack(target, model);
            cam_log("    targetPositionTrackIdx=%d\n", cam.targetPositionTrackIndex);
        }

        // Rotation track: read camera's roll angle over time (around view vector).
        // Simple approach: skip for now unless needed. WC3 target cameras mostly
        // use only position + targetPosition, not roll.
        cam.rotationTrackIndex = -1;

        model.cameras.push_back(std::move(cam));
        found++;
    }

    cam_log("Found %d camera(s) total\n", found);
}

} // namespace mdx_extract
