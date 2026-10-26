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
#include "controller_track_helper.h"
#include "visibility_track_helper.h"
#include "../mdx_class_ids.h"
#include <extraction/camera_extractor.h>   // core::CameraExtractor
#include <scene/paramblock_reader.h>

#include <max.h>
#include <object.h>
#include <gencam.h>
#include <inode.h>
#include <control.h>
#include <istdplug.h>
#include <modstack.h>

#include <algorithm>
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

// How far the exported straight line may miss the Max curve: the bone
// translation tolerance (anim_dispatcher kCurveTolTranslation).
constexpr float kCurveTolPosition = 0.01f;

// Camera / target position track (KCTR / KTTR), in the world like the static
// position CameraExtractor reads. The game plays it Linear from key to key.
// Taking only the controller's key values drew a straight line between them:
// the cameras of arthasillidanfight.mdx fly Hermite/Bezier arcs, and the
// export missed them by up to 27 units (01 Standoff has two keys 12 s apart).
// Every sequence is probed per frame plus at the controller's own key times
// (imported keys sit between frames); a probe is kept where the straight line
// from the last kept one would miss the curve by more than the tolerance
// (the greedy reduction of anim_dispatcher densifyCurveTimes). Sampling the
// node TM also covers Position XYZ, constraints and linked cameras.
// Returns -1 if the node does not move, else index into model.vec3Tracks.
int32_t extractPositionTrack(INode* node, const std::vector<ir::Sequence>& sequences,
                             ir::IRModel& model) {
    if (!node) return -1;
    const Point3 rest = node->GetNodeTM(0).GetTrans();
    const TimeValue kFrame = std::max<TimeValue>(GetTicksPerFrame(), 1);

    std::vector<TimeValue> keyTimes;
    if (Control* tm = node->GetTMController())
        if (Control* pos = tm->GetPositionController())
            for (int i = 0, n = pos->NumKeys(); i < n; ++i)
                keyTimes.push_back(pos->GetKeyTime(i));

    ir::Track<Point3> track;
    track.interpolation = ir::InterpolationType::Linear;
    bool moves = false;
    std::vector<TimeValue> st;
    std::vector<Point3> sv;
    for (const auto& seq : sequences) {
        if (seq.endTime < seq.startTime) continue;
        st.clear();
        for (TimeValue t = seq.startTime; t < seq.endTime; t += kFrame) st.push_back(t);
        st.push_back(seq.endTime);
        for (TimeValue t : keyTimes)
            if (t > seq.startTime && t < seq.endTime) st.push_back(t);
        std::sort(st.begin(), st.end());
        st.erase(std::unique(st.begin(), st.end()), st.end());
        sv.clear();
        for (TimeValue t : st) {
            sv.push_back(node->GetNodeTM(t).GetTrans());
            if (Length(sv.back() - rest) > 0.0001f) moves = true;
        }

        auto keep = [&](size_t i) {
            ir::Keyframe<Point3> k;
            k.time = st[i];
            k.value = sv[i];
            track.keys.push_back(k);
        };
        keep(0);
        size_t anchor = 0;
        for (size_t j = 1; j + 1 < st.size(); ++j) {
            const size_t to = j + 1;
            const float dt = static_cast<float>(st[to] - st[anchor]);
            bool straight = true;
            for (size_t k = anchor + 1; straight && k <= j; ++k) {
                const float u = static_cast<float>(st[k] - st[anchor]) / dt;
                straight = Length(sv[k] - (sv[anchor] + (sv[to] - sv[anchor]) * u)) <= kCurveTolPosition;
            }
            if (!straight) {
                keep(j);
                anchor = j;
            }
        }
        if (st.size() > 1) keep(st.size() - 1);
    }
    if (!moves || track.keys.empty()) return -1;

    int32_t idx = static_cast<int32_t>(model.vec3Tracks.size());
    model.vec3Tracks.push_back(std::move(track));
    return idx;
}

// A float track holding one key at frame 0.
int32_t constantFloatTrack(float value, ir::IRModel& model) {
    ir::FloatTrack track;
    track.interpolation = ir::InterpolationType::Linear;
    ir::Keyframe<float> key;
    key.time = 0;
    key.value = value;
    track.keys.push_back(key);
    int32_t idx = static_cast<int32_t>(model.floatTracks.size());
    model.floatTracks.push_back(std::move(track));
    return idx;
}

// IDUF / ELAF / PTSF from a Physical Camera. Its focus distance, lens focal
// length (mm) and f-number are the three values the 3.0.0 client reads, one
// to one. MDX has tracks and no static fields for them, and the game only
// applies depth of field when all three tracks exist, so:
//   - an animated parameter always becomes a track;
//   - with "Enable Depth of Field" on, a static parameter becomes a one-key
//     track, so the camera keeps its DoF in game.
// Target / Free cameras have none of these and export no DoF.
void extractPhysicalCameraDof(INode* node, ir::Camera& cam, ir::IRModel& model) {
    using PBR = core::ParamBlockReader;
    Object* obj = node->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
        obj = static_cast<IDerivedObject*>(obj)->GetObjRef();
    if (!obj || obj->ClassID() != mdx_ids::PHYSICAL_CAMERA) return;

    // With "Specify FOV" on, the fov parameter (degrees) is the authored
    // value. EvalCameraState derives the FOV again from the lens and drifts:
    // an imported 0.6 rad came back as 0.600056 on Max 2027, 0.600006 on 2016.
    BOOL specifyFov = FALSE;
    float fovDegrees = 0.0f;
    if (PBR::readBoolByName(obj, L"specify_fov", 0, specifyFov) && specifyFov &&
        PBR::readFloatByName(obj, L"fov", 0, fovDegrees))
        cam.fov = fovDegrees * 3.14159265358979f / 180.0f;

    using core::anim::getParamControllerDirect;
    cam.focalLengthTrackIndex = extractFloatControllerTrack(
        getParamControllerDirect(obj, L"focal_length_mm"), model);
    cam.fStopTrackIndex = extractFloatControllerTrack(
        getParamControllerDirect(obj, L"f_number"), model);

    // "Use target distance" focuses on the target, so the target distance is
    // the focus distance then.
    int specifyFocus = 1;
    PBR::readIntByName(obj, L"specify_focus", 0, specifyFocus);
    const wchar_t* focusParam = specifyFocus ? L"focus_distance" : L"target_distance";
    cam.focusDistanceTrackIndex = extractFloatControllerTrack(
        getParamControllerDirect(obj, focusParam), model);

    BOOL useDof = FALSE;
    PBR::readBoolByName(obj, L"use_dof", 0, useDof);
    if (!useDof) return;

    auto fillStatic = [&](int32_t& trackIndex, const wchar_t* param) {
        if (trackIndex >= 0) return;
        float value = 0.0f;
        if (PBR::readFloatByName(obj, param, 0, value))
            trackIndex = constantFloatTrack(value, model);
    };
    fillStatic(cam.focalLengthTrackIndex, L"focal_length_mm");
    fillStatic(cam.fStopTrackIndex, L"f_number");
    if (cam.focusDistanceTrackIndex < 0) {
        float value = 0.0f;
        if (specifyFocus) {
            PBR::readFloatByName(obj, L"focus_distance", 0, value);
        } else if (INode* target = node->GetTarget()) {
            value = Length(target->GetNodeTM(0).GetTrans() - node->GetNodeTM(0).GetTrans());
        } else {
            PBR::readFloatByName(obj, L"target_distance", 0, value);
        }
        cam.focusDistanceTrackIndex = constantFloatTrack(value, model);
    }
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
        cam.positionTrackIndex = extractPositionTrack(sn.maxNode, model.sequences, model);
        cam_log("    positionTrackIdx=%d\n", cam.positionTrackIndex);

        INode* target = sn.maxNode->GetTarget();
        if (target) {
            cam.targetPositionTrackIndex = extractPositionTrack(target, model.sequences, model);
            cam_log("    targetPositionTrackIdx=%d\n", cam.targetPositionTrackIndex);
        }

        // KCRL: roll lives on the LookAt controller of a target camera, which
        // is where the importer puts it. Its values are radians, like MDX.
        if (Control* tm = sn.maxNode->GetTMController()) {
            if (tm->ClassID() == Class_ID(LOOKAT_CONTROL_CLASS_ID, 0))
                cam.rotationTrackIndex = extractFloatControllerTrack(
                    tm->GetRollController(), model);
        }
        cam_log("    rollTrackIdx=%d\n", cam.rotationTrackIndex);

        // KCVS: the node's visibility track. A key at 0 turns the camera off
        // in the 3.0.0 client.
        cam.visibilityTrackIndex = extractVisibilityTrack(sn.maxNode, model);

        extractPhysicalCameraDof(sn.maxNode, cam, model);
        cam_log("    KCVS=%d IDUF=%d ELAF=%d PTSF=%d\n", cam.visibilityTrackIndex,
                cam.focusDistanceTrackIndex, cam.focalLengthTrackIndex,
                cam.fStopTrackIndex);

        model.cameras.push_back(std::move(cam));
        found++;
    }

    cam_log("Found %d camera(s) total\n", found);
}

} // namespace mdx_extract
