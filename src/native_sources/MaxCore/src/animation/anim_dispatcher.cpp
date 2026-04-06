// MaxCore — Animation dispatcher implementation
#include "anim_dispatcher.h"
#include "controller_reader.h"
#include "fk_sampler.h"
#include "biped_sampler.h"
#include "ik_sampler.h"
#include "cat_sampler.h"
#include "link_constraint_sampler.h"
#include "subsample_engine.h"

namespace core {

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

    for (size_t nodeIdx = 0; nodeIdx < irModel.nodes.size(); ++nodeIdx) {
        auto& irNode = irModel.nodes[nodeIdx];
        INode* maxNode = irNode.maxNode;
        if (!maxNode) continue;

        INode* parentNode = maxNode->GetParentNode();
        Control* tmCtrl = maxNode->GetTMController();
        if (!tmCtrl) continue;

        // Per-sequence animation: for now, combine all sequences into one track set.
        // MDX stores keys with absolute tick values; sequences define time ranges.
        for (const auto& seq : sequences) {
            ir::NodeAnimation nodeAnim;
            nodeAnim.nodeIndex = static_cast<int32_t>(nodeIdx);

            // Determine sampler priority
            if (ControllerReader::isLinkConstraint(tmCtrl)) {
                linkSampler.sample(maxNode, parentNode,
                                    seq.startTime, seq.endTime,
                                    nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
            } else if (ControllerReader::isBiped(maxNode)) {
                bipedSampler.sample(maxNode, parentNode,
                                     seq.startTime, seq.endTime,
                                     nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
            } else if (ControllerReader::isIKAffected(maxNode)) {
                ikSampler.sample(maxNode, parentNode,
                                  seq.startTime, seq.endTime,
                                  config.angleThreshold,
                                  nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
            } else if (ControllerReader::isCAT(maxNode)) {
                catSampler.sample(maxNode, parentNode,
                                   seq.startTime, seq.endTime,
                                   nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
            } else {
                // Try FK first
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
                    // Dense fallback
                    SubsampleEngine::SampleConfig sampleCfg;
                    sampleCfg.tickInterval = config.tickInterval;
                    sampleCfg.adaptiveRefine = false;
                    SubsampleEngine::sampleNode(maxNode, parentNode,
                                                 seq.startTime, seq.endTime, sampleCfg,
                                                 nodeAnim.translation, nodeAnim.rotation,
                                                 nodeAnim.scale);
                }
            }

            // Only add if there's actual animation data
            if (!nodeAnim.translation.empty() || !nodeAnim.rotation.empty() ||
                !nodeAnim.scale.empty()) {
                irModel.nodeAnimations.push_back(std::move(nodeAnim));
            }
        }
    }
}

} // namespace core
