// MaxCore — Animation dispatcher implementation
#include "anim_dispatcher.h"
#include "controller_reader.h"
#include "fk_sampler.h"
#include "biped_sampler.h"
#include "ik_sampler.h"
#include "cat_sampler.h"
#include "link_constraint_sampler.h"
#include "subsample_engine.h"

#include <cmath>
#include <fstream>

// Debug logging — writes to %TEMP%\mdlx_anim_debug.log
static std::ofstream& animLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_anim_debug.log";
        log.open(path, std::ios::trunc);
    }
    return log;
}
#define ALOG animLog()
#define AFLUSH animLog().flush()

namespace core {

// ── Helper: check if a value is "identity" (no real animation) ──

static bool isZeroVec(const Point3& v, float eps = 0.0001f) {
    return fabsf(v.x) < eps && fabsf(v.y) < eps && fabsf(v.z) < eps;
}

static bool isIdentityQuat(const Quat& q, float eps = 0.0001f) {
    return fabsf(q.x) < eps && fabsf(q.y) < eps &&
           fabsf(q.z) < eps && fabsf(fabsf(q.w) - 1.0f) < eps;
}

static bool isIdentityScale(const Point3& s, float eps = 0.001f) {
    return fabsf(s.x - 1.0f) < eps && fabsf(s.y - 1.0f) < eps && fabsf(s.z - 1.0f) < eps;
}

// Slerp between two quaternions
static Quat slerpQuat(const Quat& a, const Quat& b, float t) {
    float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    Quat bb = b;
    if (dot < 0.0f) { bb.x=-bb.x; bb.y=-bb.y; bb.z=-bb.z; bb.w=-bb.w; dot=-dot; }
    if (dot > 0.9999f) {
        // Nearly identical — lerp + normalize
        Quat r;
        r.x = a.x + t*(bb.x - a.x);
        r.y = a.y + t*(bb.y - a.y);
        r.z = a.z + t*(bb.z - a.z);
        r.w = a.w + t*(bb.w - a.w);
        float len = sqrtf(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
        if (len > 0.0f) { r.x/=len; r.y/=len; r.z/=len; r.w/=len; }
        return r;
    }
    float theta = acosf(dot);
    float sinTheta = sinf(theta);
    float wa = sinf((1.0f-t)*theta) / sinTheta;
    float wb = sinf(t*theta) / sinTheta;
    Quat r;
    r.x = wa*a.x + wb*bb.x;
    r.y = wa*a.y + wb*bb.y;
    r.z = wa*a.z + wb*bb.z;
    r.w = wa*a.w + wb*bb.w;
    return r;
}

// Quaternion distance (angle between two quaternions)
static float quatError(const Quat& a, const Quat& b) {
    float dot = fabsf(a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w);
    if (dot > 1.0f) dot = 1.0f;
    return 2.0f * acosf(dot); // angle in radians
}

// Remove redundant keys from a rotation track.
// Keys that can be reconstructed by slerp interpolation within the given
// angular tolerance are discarded — this matches NeoDex's key reduction behavior.
static void reduceRotationKeys(ir::QuatTrack& track, float toleranceRad = 0.001f) {
    if (track.keys.size() <= 2) return;

    std::vector<bool> keep(track.keys.size(), false);
    keep.front() = true;
    keep.back() = true;

    // Greedy forward pass: walk from first kept key, skip keys that
    // can be interpolated, keep a key when error exceeds threshold.
    size_t anchor = 0;
    for (size_t i = 1; i < track.keys.size() - 1; ++i) {
        // Check if ALL keys from anchor+1..i can be interpolated from anchor..i+1
        bool canSkip = true;
        size_t next = i + 1;
        for (size_t j = anchor + 1; j <= i; ++j) {
            float tTotal = static_cast<float>(track.keys[next].time - track.keys[anchor].time);
            float tJ = static_cast<float>(track.keys[j].time - track.keys[anchor].time);
            float frac = (tTotal > 0.0f) ? (tJ / tTotal) : 0.0f;
            Quat interp = slerpQuat(track.keys[anchor].value, track.keys[next].value, frac);
            if (quatError(interp, track.keys[j].value) > toleranceRad) {
                canSkip = false;
                break;
            }
        }
        if (!canSkip) {
            keep[i] = true;
            anchor = i;
        }
    }

    // Compact
    std::vector<ir::Keyframe<Quat>> reduced;
    for (size_t i = 0; i < track.keys.size(); ++i) {
        if (keep[i]) reduced.push_back(track.keys[i]);
    }
    track.keys = std::move(reduced);
}

// Remove redundant keys from a translation track (linear interpolation check).
static void reduceTranslationKeys(ir::Vec3Track& track, float tolerance = 0.01f) {
    if (track.keys.size() <= 2) return;

    std::vector<bool> keep(track.keys.size(), false);
    keep.front() = true;
    keep.back() = true;

    size_t anchor = 0;
    for (size_t i = 1; i < track.keys.size() - 1; ++i) {
        bool canSkip = true;
        size_t next = i + 1;
        for (size_t j = anchor + 1; j <= i; ++j) {
            float tTotal = static_cast<float>(track.keys[next].time - track.keys[anchor].time);
            float tJ = static_cast<float>(track.keys[j].time - track.keys[anchor].time);
            float frac = (tTotal > 0.0f) ? (tJ / tTotal) : 0.0f;
            Point3 interp = track.keys[anchor].value + frac * (track.keys[next].value - track.keys[anchor].value);
            Point3 diff = interp - track.keys[j].value;
            if (Length(diff) > tolerance) {
                canSkip = false;
                break;
            }
        }
        if (!canSkip) {
            keep[i] = true;
            anchor = i;
        }
    }

    std::vector<ir::Keyframe<Point3>> reduced;
    for (size_t i = 0; i < track.keys.size(); ++i) {
        if (keep[i]) reduced.push_back(track.keys[i]);
    }
    track.keys = std::move(reduced);
}

// Check if ALL keys in a track are identity (no real animation content)
static bool isTrackAllZero(const ir::Vec3Track& track) {
    for (auto& key : track.keys) {
        if (!isZeroVec(key.value)) return false;
    }
    return true;
}

static bool isTrackAllIdentityRot(const ir::QuatTrack& track) {
    for (auto& key : track.keys) {
        if (!isIdentityQuat(key.value)) return false;
    }
    return true;
}

static bool isTrackAllIdentityScale(const ir::Vec3Track& track) {
    for (auto& key : track.keys) {
        if (!isIdentityScale(key.value)) return false;
    }
    return true;
}

void AnimDispatcher::bakeAll(ir::IRModel& irModel,
                              const std::vector<ir::Sequence>& sequences,
                              const Config& config,
                              ExportErrorReporter& reporter) {
    if (sequences.empty()) return;

    FKSampler fkSampler;
    BipedSampler bipedSampler;
    IKSampler ikSampler;
    CATSampler catSampler;
    LinkConstraintSampler linkSampler;

    ALOG << "=== AnimDispatcher::bakeAll === nodes=" << irModel.nodes.size()
         << " sequences=" << sequences.size() << "\n";
    ALOG << "  Using GetValue() approach (Autodesk-recommended)\n";
    AFLUSH;

    for (size_t nodeIdx = 0; nodeIdx < irModel.nodes.size(); ++nodeIdx) {
        auto& irNode = irModel.nodes[nodeIdx];
        INode* maxNode = irNode.maxNode;
        if (!maxNode) continue;

        INode* parentNode = maxNode->GetParentNode();
        Control* tmCtrl = maxNode->GetTMController();
        if (!tmCtrl) continue;

        // ── Sample bind pose at frame 0 (tick 0) ──
        Point3 bindPos, bindScl;
        Quat   bindRot;

        ALOG << "  >> evaluating bind pose for node[" << nodeIdx << "] '" << irNode.name << "'...\n";
        AFLUSH;

        // Biped controllers crash on GetValue() — use GetNodeTM decomposition instead.
        // Link Constraint nodes also need GetNodeTM because their effective parent
        // changes during animation, making GetValue unreliable for bind pose.
        // CAT controllers can return incorrect rotations via GetValue (known bug).
        bool isBipedNode = ControllerReader::isBiped(maxNode);
        bool isLinkNode = ControllerReader::isLinkConstraint(tmCtrl);
        // Fallback: check ClassID directly (Link Constraint ClassID not always recognized)
        if (!isLinkNode && tmCtrl) {
            Class_ID lcID(2269112164u, 2864612865u);
            isLinkNode = (tmCtrl->ClassID() == lcID);
        }
        bool isCATNode = ControllerReader::isCAT(maxNode);
        bool needsSafeBindPose = isBipedNode || isLinkNode || isCATNode;
        if (needsSafeBindPose) {
            // Safe path: decompose GetNodeTM (same as IK path in subsample_engine)
            Matrix3 nodeTM = maxNode->GetNodeTM(0);
            Matrix3 localTM;
            INode* root = GetCOREInterface()->GetRootNode();
            if (parentNode && parentNode != root) {
                Matrix3 parentTM = parentNode->GetNodeTM(0);
                localTM = nodeTM * Inverse(parentTM);
            } else {
                localTM = nodeTM;
            }
            bindPos = localTM.GetRow(3);
            Point3 r0 = localTM.GetRow(0);
            Point3 r1 = localTM.GetRow(1);
            Point3 r2 = localTM.GetRow(2);
            float sx = Length(r0); float sy = Length(r1); float sz = Length(r2);
            if (sx < 0.0001f) sx = 1.0f;
            if (sy < 0.0001f) sy = 1.0f;
            if (sz < 0.0001f) sz = 1.0f;
            bindScl = Point3(sx, sy, sz);
            if (sx > 0.0001f) r0 = r0 / sx;
            if (sy > 0.0001f) r1 = r1 / sy;
            if (sz > 0.0001f) r2 = r2 / sz;
            Matrix3 rotMat;
            rotMat.SetRow(0, r0); rotMat.SetRow(1, r1);
            rotMat.SetRow(2, r2); rotMat.SetRow(3, Point3(0,0,0));
            bindRot = Quat(rotMat);
        } else {
            SubsampleEngine::evaluateLocalTransform(maxNode, parentNode, 0, bindPos, bindRot, bindScl);
        }

        ALOG << "  >> bind pose OK\n";
        AFLUSH;

        // Always resample rotation via GetNodeTM world transforms.
        // Bones can have identity bind rotation yet significant animation
        // (e.g. spine_03_bind_jnt in book golem). The FK sampler alone
        // produces too few keys — GetNodeTM captures every frame correctly.
        bool needsRotDelta = true;
        bool needsScaleDelta = !isIdentityScale(bindScl);

        // ── World rotation bind poses for NeoDex-style delta ──
        // Uses GetNodeTM() which is universally correct for FK, IK, constraints, etc.
        Quat worldBindRot(0.0f,0.0f,0.0f,1.0f), parentWorldBindRot(0.0f,0.0f,0.0f,1.0f);
        if (needsRotDelta) {
            Matrix3 wb = maxNode->GetNodeTM(0);
            wb.NoTrans();
            Point3 r0 = Normalize(wb.GetRow(0));
            Point3 r1 = Normalize(wb.GetRow(1));
            Point3 r2 = Normalize(wb.GetRow(2));
            worldBindRot = Quat(Matrix3(r0, r1, r2, Point3(0,0,0)));

            if (parentNode && !parentNode->IsRootNode()) {
                Matrix3 pb = parentNode->GetNodeTM(0);
                pb.NoTrans();
                r0 = Normalize(pb.GetRow(0));
                r1 = Normalize(pb.GetRow(1));
                r2 = Normalize(pb.GetRow(2));
                parentWorldBindRot = Quat(Matrix3(r0, r1, r2, Point3(0,0,0)));
            }
        }

        ALOG << "  node[" << nodeIdx << "] '" << irNode.name << "'"
             << " bindPos=(" << bindPos.x << "," << bindPos.y << "," << bindPos.z << ")"
             << " bindRot=(" << bindRot.x << "," << bindRot.y << "," << bindRot.z << "," << bindRot.w << ")"
             << " bindScl=(" << bindScl.x << "," << bindScl.y << "," << bindScl.z << ")"
             << " rotDelta=" << (needsRotDelta ? "ACTIVE" : "none(identity)")
             << "\n";

        // Collect all sequence animations for this node
        std::vector<ir::NodeAnimation> nodeAnims;
        bool hasAnyRealTranslation = false;
        bool hasAnyRealRotation = false;
        bool hasAnyRealScale = false;

        for (const auto& seq : sequences) {
            ir::NodeAnimation nodeAnim;
            nodeAnim.nodeIndex = static_cast<int32_t>(nodeIdx);

            // Determine sampler priority
            if (isLinkNode) {
                linkSampler.sample(maxNode, parentNode,
                                    seq.startTime, seq.endTime,
                                    nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
            } else if (isBipedNode) {
                bipedSampler.sample(maxNode, parentNode,
                                     seq.startTime, seq.endTime,
                                     nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
            } else if (ControllerReader::isIKAffected(maxNode)) {
                ikSampler.sample(maxNode, parentNode,
                                  seq.startTime, seq.endTime,
                                  config.angleThreshold,
                                  nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
            } else if (isCATNode) {
                catSampler.sample(maxNode, parentNode,
                                   seq.startTime, seq.endTime,
                                   nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
            } else {
                Control* posCtrl = tmCtrl->GetPositionController();
                Control* rotCtrl = tmCtrl->GetRotationController();
                ControllerType posType = ControllerReader::detect(posCtrl);
                ControllerType rotType = ControllerReader::detect(rotCtrl);

                bool isFKPos = (posType == ControllerType::Bezier_Position ||
                                posType == ControllerType::TCB_Position ||
                                posType == ControllerType::Linear_Position);
                bool isFKRot = (rotType == ControllerType::Bezier_Rotation ||
                                rotType == ControllerType::TCB_Rotation ||
                                rotType == ControllerType::Linear_Rotation ||
                                rotType == ControllerType::Euler_XYZ);

                if (isFKPos || isFKRot) {
                    fkSampler.sample(maxNode, parentNode,
                                      seq.startTime, seq.endTime,
                                      nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
                } else {
                    SubsampleEngine::SampleConfig sampleCfg;
                    sampleCfg.tickInterval = config.tickInterval;
                    sampleCfg.adaptiveRefine = false;
                    SubsampleEngine::sampleNode(maxNode, parentNode,
                                                 seq.startTime, seq.endTime, sampleCfg,
                                                 nodeAnim.translation, nodeAnim.rotation,
                                                 nodeAnim.scale);
                }
            }

            // ── Translation delta using world TMs ──
            // Computes local position using scale-normalized parent TM.
            // This ensures translation deltas are in the same scale as pivot points,
            // even when parent nodes have non-unit scale (e.g. 151x).
            {
                // Log what the FK sampler produced BEFORE we replace it
                int fkKeyCount = static_cast<int>(nodeAnim.translation.keys.size());
                Point3 fkFirstVal(0,0,0), fkLastVal(0,0,0);
                if (fkKeyCount > 0) {
                    fkFirstVal = nodeAnim.translation.keys[0].value;
                    fkLastVal = nodeAnim.translation.keys.back().value;
                }

                nodeAnim.translation.keys.clear();
                nodeAnim.translation.interpolation = ir::InterpolationType::Linear;
                const int frameInterval = 160;

                // Precompute bind-time parent (scale-normalized)
                Point3 worldBindPos = maxNode->GetNodeTM(0).GetTrans();
                Point3 localBindPos = worldBindPos; // fallback for root bones

                // Parent scale info for debug
                float parentRowLen = 1.0f;

                // Parent bind rotation matrix — needed to convert delta from
                // Max parent-local space to MDX parent-local space.
                Matrix3 parentBindRot;
                parentBindRot.IdentityMatrix();

                if (parentNode && !parentNode->IsRootNode()) {
                    Matrix3 pb = parentNode->GetNodeTM(0);
                    Point3 pbPos = pb.GetTrans();
                    pb.NoTrans();
                    parentRowLen = Length(pb.GetRow(0));
                    pb.SetRow(0, Normalize(pb.GetRow(0)));
                    pb.SetRow(1, Normalize(pb.GetRow(1)));
                    pb.SetRow(2, Normalize(pb.GetRow(2)));
                    localBindPos = (worldBindPos - pbPos) * Inverse(pb);
                    parentBindRot = pb;
                }

                ALOG << "    TRANS node[" << nodeIdx << "] '" << irNode.name << "'"
                     << " fkKeys=" << fkKeyCount
                     << " fkFirst=(" << fkFirstVal.x << "," << fkFirstVal.y << "," << fkFirstVal.z << ")"
                     << " fkLast=(" << fkLastVal.x << "," << fkLastVal.y << "," << fkLastVal.z << ")"
                     << "\n";
                ALOG << "      worldBindPos=(" << worldBindPos.x << "," << worldBindPos.y << "," << worldBindPos.z << ")"
                     << " localBindPos=(" << localBindPos.x << "," << localBindPos.y << "," << localBindPos.z << ")"
                     << " parentScale=" << parentRowLen
                     << " evalLocalBind=(" << bindPos.x << "," << bindPos.y << "," << bindPos.z << ")"
                     << "\n";

                int transLogCount = 0;
                for (TimeValue t = seq.startTime; t <= seq.endTime; t += frameInterval) {
                    Point3 worldPos = maxNode->GetNodeTM(t).GetTrans();
                    Point3 localPos = worldPos; // fallback for root bones

                    if (parentNode && !parentNode->IsRootNode()) {
                        Matrix3 pt = parentNode->GetNodeTM(t);
                        Point3 ptPos = pt.GetTrans();
                        pt.NoTrans();
                        pt.SetRow(0, Normalize(pt.GetRow(0)));
                        pt.SetRow(1, Normalize(pt.GetRow(1)));
                        pt.SetRow(2, Normalize(pt.GetRow(2)));
                        localPos = (worldPos - ptPos) * Inverse(pt);
                    }

                    Point3 delta = localPos - localBindPos;

                    // Convert from Max parent-local to MDX parent-local space
                    delta = delta * parentBindRot;

                    if (fabsf(delta.x) < 0.0001f) delta.x = 0.0f;
                    if (fabsf(delta.y) < 0.0001f) delta.y = 0.0f;
                    if (fabsf(delta.z) < 0.0001f) delta.z = 0.0f;

                    // Log first 5 frames and every 100th frame
                    if (transLogCount < 5 || (t % (frameInterval * 100)) == 0) {
                        ALOG << "      t=" << t
                             << " world=(" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")"
                             << " local=(" << localPos.x << "," << localPos.y << "," << localPos.z << ")"
                             << " delta=(" << delta.x << "," << delta.y << "," << delta.z << ")"
                             << "\n";
                    }
                    transLogCount++;

                    ir::Keyframe<Point3> key;
                    key.time = t;
                    key.value = delta;
                    nodeAnim.translation.keys.push_back(key);
                }

                ALOG << "      totalTransKeys=" << nodeAnim.translation.keys.size() << "\n";
            }

            // ── Rotation delta using world TMs (NeoDex method) ──
            // Resample at EVERY FRAME using GetNodeTM() world transforms.
            // NeoDex samples all bones at every frame — GetNodeTM() captures
            // IK, constraints, and all controller effects correctly.
            if (needsRotDelta) {
                nodeAnim.rotation.keys.clear();
                nodeAnim.rotation.interpolation = ir::InterpolationType::Linear;

                Quat invWorldBind = Inverse(worldBindRot);
                Quat invParentWorldBind = Inverse(parentWorldBindRot);
                int logCount = 0;
                const int frameInterval = 160; // 1 frame at 30fps

                for (TimeValue t = seq.startTime; t <= seq.endTime; t += frameInterval) {
                    Matrix3 wt = maxNode->GetNodeTM(t);
                    wt.NoTrans();
                    Point3 r0 = Normalize(wt.GetRow(0));
                    Point3 r1 = Normalize(wt.GetRow(1));
                    Point3 r2 = Normalize(wt.GetRow(2));
                    Quat worldRot = Quat(Matrix3(r0, r1, r2, Point3(0,0,0)));

                    Quat parentWorldRot(0.0f,0.0f,0.0f,1.0f);
                    if (parentNode && !parentNode->IsRootNode()) {
                        Matrix3 pt = parentNode->GetNodeTM(t);
                        pt.NoTrans();
                        r0 = Normalize(pt.GetRow(0));
                        r1 = Normalize(pt.GetRow(1));
                        r2 = Normalize(pt.GetRow(2));
                        parentWorldRot = Quat(Matrix3(r0, r1, r2, Point3(0,0,0)));
                    }

                    Quat worldDelta = invWorldBind * worldRot;
                    Quat parentDelta = invParentWorldBind * parentWorldRot;
                    Quat result = worldDelta * Inverse(parentDelta);

                    // Hemisphere consistency: force w >= 0, then check prev-key
                    if (result.w < 0.0f) {
                        result.x = -result.x; result.y = -result.y;
                        result.z = -result.z; result.w = -result.w;
                    }
                    if (!nodeAnim.rotation.keys.empty()) {
                        const Quat& prev = nodeAnim.rotation.keys.back().value;
                        if ((prev.x*result.x + prev.y*result.y + prev.z*result.z + prev.w*result.w) < 0.0f) {
                            result.x = -result.x; result.y = -result.y;
                            result.z = -result.z; result.w = -result.w;
                        }
                    }

                    ir::Keyframe<Quat> key;
                    key.time = t;
                    key.value = result;
                    nodeAnim.rotation.keys.push_back(key);

                    if (logCount < 6) {
                        ALOG << "    ROT t=" << t
                             << " worldRot=(" << worldRot.x << "," << worldRot.y << "," << worldRot.z << "," << worldRot.w << ")"
                             << " wDelta=(" << worldDelta.x << "," << worldDelta.y << "," << worldDelta.z << "," << worldDelta.w << ")"
                             << " pDelta=(" << parentDelta.x << "," << parentDelta.y << "," << parentDelta.z << "," << parentDelta.w << ")"
                             << " result=(" << result.x << "," << result.y << "," << result.z << "," << result.w << ")\n";
                        logCount++;
                    }
                }
            }
            // Snap near-identity rotation components
            for (auto& key : nodeAnim.rotation.keys) {
                if (fabsf(key.value.x) < 0.00001f) key.value.x = 0.0f;
                if (fabsf(key.value.y) < 0.00001f) key.value.y = 0.0f;
                if (fabsf(key.value.z) < 0.00001f) key.value.z = 0.0f;
                if (fabsf(key.value.w - 1.0f) < 0.00001f) key.value.w = 1.0f;
                if (fabsf(key.value.w + 1.0f) < 0.00001f) key.value.w = -1.0f;
            }

            // ── Scale: resample using GetNodeTM (same approach as rotation) ──
            // The FK sampler may miss keys or sample across sequence boundaries.
            // Resampling via GetNodeTM at every frame captures the true scale.
            {
                nodeAnim.scale.keys.clear();
                nodeAnim.scale.interpolation = ir::InterpolationType::Linear;
                const int frameInterval = 160;

                // Bind-time local scale (row lengths of local TM)
                Matrix3 bindNodeTM = maxNode->GetNodeTM(0);
                Matrix3 bindParentTM;
                bindParentTM.IdentityMatrix();
                if (parentNode && !parentNode->IsRootNode())
                    bindParentTM = parentNode->GetNodeTM(0);
                Matrix3 bindLocalTM = bindNodeTM * Inverse(bindParentTM);
                float bindSx = Length(bindLocalTM.GetRow(0));
                float bindSy = Length(bindLocalTM.GetRow(1));
                float bindSz = Length(bindLocalTM.GetRow(2));
                if (bindSx < 0.0001f) bindSx = 1.0f;
                if (bindSy < 0.0001f) bindSy = 1.0f;
                if (bindSz < 0.0001f) bindSz = 1.0f;

                for (TimeValue t = seq.startTime; t <= seq.endTime; t += frameInterval) {
                    Matrix3 nodeTM = maxNode->GetNodeTM(t);
                    Matrix3 parTM;
                    parTM.IdentityMatrix();
                    if (parentNode && !parentNode->IsRootNode())
                        parTM = parentNode->GetNodeTM(t);
                    Matrix3 localTM = nodeTM * Inverse(parTM);

                    float sx = Length(localTM.GetRow(0)) / bindSx;
                    float sy = Length(localTM.GetRow(1)) / bindSy;
                    float sz = Length(localTM.GetRow(2)) / bindSz;

                    if (fabsf(sx - 1.0f) < 0.001f) sx = 1.0f;
                    if (fabsf(sy - 1.0f) < 0.001f) sy = 1.0f;
                    if (fabsf(sz - 1.0f) < 0.001f) sz = 1.0f;

                    ir::Keyframe<Point3> key;
                    key.time = t;
                    key.value = Point3(sx, sy, sz);
                    nodeAnim.scale.keys.push_back(key);
                }
            }

            // ── Key reduction: remove keys reconstructable by interpolation ──
            // Tolerances tuned for WC3 MDX quality:
            //   Rotation: 0.01 rad ≈ 0.57° — barely visible, good reduction
            //   Translation: 0.05 units — adequate for scenes up to 300 units
            //   Scale: 0.005 — 0.5% deviation allowed
            {
                size_t trBefore = nodeAnim.translation.keys.size();
                size_t rtBefore = nodeAnim.rotation.keys.size();
                size_t scBefore = nodeAnim.scale.keys.size();

                reduceTranslationKeys(nodeAnim.translation, 0.05f);

                // Detect multi-revolution rotation (>360°) by summing
                // frame-to-frame angular changes. Quaternion slerp can't
                // represent >180° per segment, so key reduction would break
                // continuous rotations like 720° spins.
                float cumulativeAngle = 0.0f;
                for (size_t k = 1; k < nodeAnim.rotation.keys.size(); ++k) {
                    const Quat& q0 = nodeAnim.rotation.keys[k-1].value;
                    const Quat& q1 = nodeAnim.rotation.keys[k].value;
                    float dot = q0.x*q1.x + q0.y*q1.y + q0.z*q1.z + q0.w*q1.w;
                    float absDot = fabsf(dot);
                    if (absDot > 1.0f) absDot = 1.0f;
                    cumulativeAngle += 2.0f * acosf(absDot);
                }
                bool isMultiRevolution = (cumulativeAngle > 6.28318f); // > 360°

                if (isMultiRevolution) {
                    ALOG << "    MULTI-REV detected: cumulative=" << (cumulativeAngle * 57.2958f)
                         << " deg — skipping rotation reduction\n";
                } else {
                    reduceRotationKeys(nodeAnim.rotation, 0.01f);
                }

                reduceTranslationKeys(nodeAnim.scale, 0.005f);

                if (trBefore > 2 || rtBefore > 2 || scBefore > 2) {
                    ALOG << "    REDUCE node[" << nodeIdx << "] '" << irNode.name << "'"
                         << " tr=" << trBefore << "->" << nodeAnim.translation.keys.size()
                         << " rt=" << rtBefore << "->" << nodeAnim.rotation.keys.size()
                         << " sc=" << scBefore << "->" << nodeAnim.scale.keys.size()
                         << "\n";
                }
            }

            // ── Check if this sequence has real animation ──
            if (!isTrackAllZero(nodeAnim.translation))
                hasAnyRealTranslation = true;
            if (!isTrackAllIdentityRot(nodeAnim.rotation))
                hasAnyRealRotation = true;
            if (!isTrackAllIdentityScale(nodeAnim.scale))
                hasAnyRealScale = true;

            // ── Strip identity-only tracks for this sequence ──
            if (isTrackAllZero(nodeAnim.translation))
                nodeAnim.translation.keys.clear();
            if (isTrackAllIdentityRot(nodeAnim.rotation))
                nodeAnim.rotation.keys.clear();
            if (isTrackAllIdentityScale(nodeAnim.scale))
                nodeAnim.scale.keys.clear();

            nodeAnims.push_back(std::move(nodeAnim));
        }

        // ── Add t=0 rest-pose key (identity) for tracks with real animation ──
        // MDX needs a key at t=0 to define the rest/default pose.
        // Without it, viewers don't know what to show when no sequence plays.
        bool needsRestKey = hasAnyRealTranslation || hasAnyRealRotation || hasAnyRealScale;

        if (needsRestKey) {
            ir::NodeAnimation restAnim;
            restAnim.nodeIndex = static_cast<int32_t>(nodeIdx);

            if (hasAnyRealTranslation) {
                ir::Keyframe<Point3> key;
                key.time = 0;
                key.value = Point3(0, 0, 0);
                restAnim.translation.keys.push_back(key);
                restAnim.translation.interpolation = ir::InterpolationType::Linear;
            }

            if (hasAnyRealRotation) {
                ir::Keyframe<Quat> key;
                key.time = 0;
                key.value = Quat(0.0f, 0.0f, 0.0f, 1.0f);
                restAnim.rotation.keys.push_back(key);
                restAnim.rotation.interpolation = ir::InterpolationType::Linear;
            }

            if (hasAnyRealScale) {
                ir::Keyframe<Point3> key;
                key.time = 0;
                key.value = Point3(1, 1, 1);
                restAnim.scale.keys.push_back(key);
                restAnim.scale.interpolation = ir::InterpolationType::Linear;
            }

            irModel.nodeAnimations.push_back(std::move(restAnim));
        }

        // ── Push sequence animations (only if they have actual data) ──
        for (auto& na : nodeAnims) {
            if (!na.translation.empty() || !na.rotation.empty() || !na.scale.empty()) {
                irModel.nodeAnimations.push_back(std::move(na));
            }
        }

        // ── Log summary ──
        ALOG << "    hasRealAnim: TR=" << hasAnyRealTranslation
             << " RT=" << hasAnyRealRotation
             << " SC=" << hasAnyRealScale
             << " restKey=" << needsRestKey << "\n";
        AFLUSH;
    }

    ALOG << "=== AnimDispatcher complete: " << irModel.nodeAnimations.size() << " nodeAnimations ===\n";
    AFLUSH;
}

} // namespace core
