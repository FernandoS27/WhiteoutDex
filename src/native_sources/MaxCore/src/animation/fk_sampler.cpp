// MaxCore — FK animation sampler implementation
#include "fk_sampler.h"
#include "controller_reader.h"
#include "subsample_engine.h"
#include "tangent_compute.h"

#include <istdplug.h>

namespace core {

bool FKSampler::sample(INode* node, INode* parent,
                        TimeValue startTime, TimeValue endTime,
                        ir::Vec3Track& outPos, ir::QuatTrack& outRot, ir::Vec3Track& outScl) {
    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return false;

    Control* posCtrl = tmCtrl->GetPositionController();
    Control* rotCtrl = tmCtrl->GetRotationController();
    Control* sclCtrl = tmCtrl->GetScaleController();

    samplePosition(posCtrl, node, parent, startTime, endTime, outPos);
    sampleRotation(rotCtrl, node, parent, startTime, endTime, outRot);
    sampleScale(sclCtrl, node, parent, startTime, endTime, outScl);

    return true;
}

void FKSampler::samplePosition(Control* posCtrl, INode* node, INode* parent,
                                 TimeValue startTime, TimeValue endTime,
                                 ir::Vec3Track& outPos) {
    if (!posCtrl) return;
    ControllerType type = ControllerReader::detect(posCtrl);

    // For all FK types: evaluate at each key time to get local-space position
    // IKeyControl provides access to raw keyframes
    IKeyControl* ikc = GetKeyControlInterface(posCtrl);
    if (!ikc || ikc->GetNumKeys() == 0) {
        // No keys — sample a single static value
        Point3 pos, scl; Quat rot;
        SubsampleEngine::evaluateLocalTransform(node, parent, startTime, pos, rot, scl);
        ir::Keyframe<Point3> key;
        key.time = startTime;
        key.value = pos;
        outPos.keys.push_back(key);
        outPos.interpolation = ir::InterpolationType::None;
        return;
    }

    // Set interpolation type based on controller
    switch (type) {
        case ControllerType::Linear_Position:
            outPos.interpolation = ir::InterpolationType::Linear;
            break;
        case ControllerType::Bezier_Position:
            outPos.interpolation = ir::InterpolationType::Bezier;
            break;
        case ControllerType::TCB_Position:
            outPos.interpolation = ir::InterpolationType::Hermite;
            break;
        default:
            outPos.interpolation = ir::InterpolationType::Linear;
            break;
    }

    // Extract keys within the sequence range
    int numKeys = ikc->GetNumKeys();
    for (int k = 0; k < numKeys; ++k) {
        TimeValue keyTime = posCtrl->GetKeyTime(k);
        if (keyTime < startTime || keyTime > endTime) continue;

        // Evaluate local transform at key time
        Point3 pos, scl; Quat rot;
        SubsampleEngine::evaluateLocalTransform(node, parent, keyTime, pos, rot, scl);

        ir::Keyframe<Point3> key;
        key.time = keyTime;
        key.value = pos;
        outPos.keys.push_back(key);
    }

    // Ensure boundary keys
    if (outPos.keys.empty() || outPos.keys.front().time != startTime) {
        Point3 pos, scl; Quat rot;
        SubsampleEngine::evaluateLocalTransform(node, parent, startTime, pos, rot, scl);
        ir::Keyframe<Point3> key;
        key.time = startTime;
        key.value = pos;
        outPos.keys.insert(outPos.keys.begin(), key);
    }
    if (outPos.keys.back().time != endTime) {
        Point3 pos, scl; Quat rot;
        SubsampleEngine::evaluateLocalTransform(node, parent, endTime, pos, rot, scl);
        ir::Keyframe<Point3> key;
        key.time = endTime;
        key.value = pos;
        outPos.keys.push_back(key);
    }

    // Compute tangents from TCB/Bezier params if needed
    if (type == ControllerType::TCB_Position && outPos.keys.size() >= 2) {
        for (size_t i = 0; i < outPos.keys.size(); ++i) {
            auto& key = outPos.keys[i];
            key.hasTangents = true;
            if (i == 0 || i == outPos.keys.size() - 1) {
                // Boundary: use forward/backward difference
                size_t other = (i == 0) ? 1 : outPos.keys.size() - 2;
                float dt = static_cast<float>(outPos.keys[other].time - key.time);
                if (fabsf(dt) > 0.0f) {
                    key.inTangent = (outPos.keys[other].value - key.value) / dt;
                    key.outTangent = key.inTangent;
                }
            } else {
                // Central difference for simplicity (TCB params not easily accessible via IKeyControl)
                key.inTangent = tangent::centralDifference(
                    outPos.keys[i - 1].value, outPos.keys[i + 1].value,
                    outPos.keys[i - 1].time, outPos.keys[i + 1].time);
                key.outTangent = key.inTangent;
            }
        }
    }
}

void FKSampler::sampleRotation(Control* rotCtrl, INode* node, INode* parent,
                                 TimeValue startTime, TimeValue endTime,
                                 ir::QuatTrack& outRot) {
    if (!rotCtrl) return;
    ControllerType type = ControllerReader::detect(rotCtrl);

    IKeyControl* ikc = GetKeyControlInterface(rotCtrl);
    if (!ikc || ikc->GetNumKeys() == 0) {
        Point3 pos, scl; Quat rot;
        SubsampleEngine::evaluateLocalTransform(node, parent, startTime, pos, rot, scl);
        ir::Keyframe<Quat> key;
        key.time = startTime;
        key.value = rot;
        outRot.keys.push_back(key);
        outRot.interpolation = ir::InterpolationType::None;
        return;
    }

    switch (type) {
        case ControllerType::Linear_Rotation:
            outRot.interpolation = ir::InterpolationType::Linear;
            break;
        case ControllerType::Bezier_Rotation:
            outRot.interpolation = ir::InterpolationType::Bezier;
            break;
        case ControllerType::TCB_Rotation:
            outRot.interpolation = ir::InterpolationType::Hermite;
            break;
        case ControllerType::Euler_XYZ:
            outRot.interpolation = ir::InterpolationType::Linear;
            break;
        default:
            outRot.interpolation = ir::InterpolationType::Linear;
            break;
    }

    int numKeys = ikc->GetNumKeys();
    for (int k = 0; k < numKeys; ++k) {
        TimeValue keyTime = rotCtrl->GetKeyTime(k);
        if (keyTime < startTime || keyTime > endTime) continue;

        Point3 pos, scl; Quat rot;
        SubsampleEngine::evaluateLocalTransform(node, parent, keyTime, pos, rot, scl);

        // Ensure shortest path
        if (!outRot.keys.empty()) {
            rot = tangent::ensureShortestPath(outRot.keys.back().value, rot);
        }

        ir::Keyframe<Quat> key;
        key.time = keyTime;
        key.value = rot;
        outRot.keys.push_back(key);
    }

    // Boundary keys
    if (outRot.keys.empty() || outRot.keys.front().time != startTime) {
        Point3 pos, scl; Quat rot;
        SubsampleEngine::evaluateLocalTransform(node, parent, startTime, pos, rot, scl);
        ir::Keyframe<Quat> key;
        key.time = startTime;
        key.value = rot;
        outRot.keys.insert(outRot.keys.begin(), key);
    }
    if (outRot.keys.back().time != endTime) {
        Point3 pos, scl; Quat rot;
        SubsampleEngine::evaluateLocalTransform(node, parent, endTime, pos, rot, scl);
        rot = tangent::ensureShortestPath(outRot.keys.back().value, rot);
        ir::Keyframe<Quat> key;
        key.time = endTime;
        key.value = rot;
        outRot.keys.push_back(key);
    }
}

void FKSampler::sampleScale(Control* sclCtrl, INode* node, INode* parent,
                              TimeValue startTime, TimeValue endTime,
                              ir::Vec3Track& outScl) {
    if (!sclCtrl) return;
    ControllerType type = ControllerReader::detect(sclCtrl);

    IKeyControl* ikc = GetKeyControlInterface(sclCtrl);
    if (!ikc || ikc->GetNumKeys() == 0) {
        Point3 pos, scl; Quat rot;
        SubsampleEngine::evaluateLocalTransform(node, parent, startTime, pos, rot, scl);
        ir::Keyframe<Point3> key;
        key.time = startTime;
        key.value = scl;
        outScl.keys.push_back(key);
        outScl.interpolation = ir::InterpolationType::None;
        return;
    }

    switch (type) {
        case ControllerType::Linear_Scale:
            outScl.interpolation = ir::InterpolationType::Linear;
            break;
        case ControllerType::Bezier_Scale:
            outScl.interpolation = ir::InterpolationType::Bezier;
            break;
        case ControllerType::TCB_Scale:
            outScl.interpolation = ir::InterpolationType::Hermite;
            break;
        default:
            outScl.interpolation = ir::InterpolationType::Linear;
            break;
    }

    int numKeys = ikc->GetNumKeys();
    for (int k = 0; k < numKeys; ++k) {
        TimeValue keyTime = sclCtrl->GetKeyTime(k);
        if (keyTime < startTime || keyTime > endTime) continue;

        Point3 pos, scl; Quat rot;
        SubsampleEngine::evaluateLocalTransform(node, parent, keyTime, pos, rot, scl);

        ir::Keyframe<Point3> key;
        key.time = keyTime;
        key.value = scl;
        outScl.keys.push_back(key);
    }

    // Boundary keys
    if (outScl.keys.empty() || outScl.keys.front().time != startTime) {
        Point3 pos, scl; Quat rot;
        SubsampleEngine::evaluateLocalTransform(node, parent, startTime, pos, rot, scl);
        ir::Keyframe<Point3> key;
        key.time = startTime;
        key.value = scl;
        outScl.keys.insert(outScl.keys.begin(), key);
    }
    if (outScl.keys.back().time != endTime) {
        Point3 pos, scl; Quat rot;
        SubsampleEngine::evaluateLocalTransform(node, parent, endTime, pos, rot, scl);
        ir::Keyframe<Point3> key;
        key.time = endTime;
        key.value = scl;
        outScl.keys.push_back(key);
    }
}

} // namespace core
