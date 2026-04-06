// MDLXExporter — BlizzFaceFX extractor implementation
#include "wc3_facefx_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>

namespace mdx_extract {

namespace {

std::string wstrToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

} // anonymous namespace

void extractFaceFX(const std::vector<core::SceneNode>& nodes,
                   ir::IRModel& model,
                   core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    for (auto& sn : nodes) {
        if (sn.customTag != "BlizzFaceFX") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        TimeValue t = 0;

        auto ext = std::make_unique<FaceFXExtensionData>();

        // Read facefxName and facefxPath from ParamBlock
        std::wstring facefxNameW, facefxPathW;
        // These are string params in the FaceFX scripted plugin
        IParamBlock2* pb = PBR::findParamBlock(ref, 0);
        if (pb) {
            const MCHAR* nameStr = nullptr;
            Interval valid = FOREVER;
            pb->GetValue(0, t, nameStr, valid); // facefxName at ParamID 0
            ext->facefxName = wstrToUtf8(nameStr);

            const MCHAR* pathStr = nullptr;
            pb->GetValue(1, t, pathStr, valid); // facefxPath at ParamID 1
            ext->facefxPath = wstrToUtf8(pathStr);
        }

        // Store on the IR node
        if (sn.nodeIndex >= 0 && sn.nodeIndex < static_cast<int32_t>(model.nodes.size())) {
            model.nodes[sn.nodeIndex].ext = std::move(ext);
        }
    }
}

} // namespace mdx_extract
