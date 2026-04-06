// MDLXExporter — Wc3Event extractor implementation
#include "wc3_event_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>
#include <iparamb2.h>

namespace mdx_extract {

void extractEvents(const std::vector<core::SceneNode>& nodes,
                   ir::IRModel& model,
                   core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3Event") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        ir::EventObject evt;
        evt.nodeIndex = sn.nodeIndex;

        // The event type/data is encoded in the node name (e.g., "SND_FAL0")
        const MCHAR* nodeName = sn.maxNode->GetName();
        if (nodeName) {
            int len = WideCharToMultiByte(CP_UTF8, 0, nodeName, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string name(static_cast<size_t>(len - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, nodeName, -1, name.data(), len, nullptr, nullptr);
                evt.eventCode = name;
            }
        }

        // Read key times from the "keyList" IntTab param
        // ParamID 0 = keyList in Wc3RefEvent.ms (or Wc3_Event)
        IParamBlock2* pb = PBR::findParamBlock(ref, 0);
        if (pb) {
            // keyList is typically ParamID 0 in the event object
            int count = pb->Count(0);
            int tpf = GetTicksPerFrame();
            for (int i = 0; i < count; i++) {
                int keyTime = 0;
                Interval valid = FOREVER;
                pb->GetValue(0, 0, keyTime, valid, i);
                evt.keyTimes.push_back(static_cast<TimeValue>(keyTime) * tpf);
            }
        }

        model.eventObjects.push_back(std::move(evt));
    }
}

} // namespace mdx_extract
