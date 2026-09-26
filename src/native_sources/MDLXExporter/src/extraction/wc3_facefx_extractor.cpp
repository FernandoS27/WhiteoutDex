// MDLXExporter — Wc3FaceFX extractor implementation
#include "wc3_facefx_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>
#include <modstack.h>

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
        if (sn.customTag != "Wc3FaceFX") continue;
        if (!sn.maxNode) continue;

        Object* obj = sn.maxNode->GetObjectRef();
        while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
            obj = static_cast<IDerivedObject*>(obj)->GetObjRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        TimeValue t = 0;

        auto ext = std::make_unique<FaceFXExtensionData>();

        // facefxName / facefxPath by name, in both the WhiteoutDex and the
        // NeoDex FaceFX plug-in: the first parameter of either is
        // "adsorption" (General rollout), not the name.
        std::wstring nameW, pathW;
        PBR::readStringByName(ref, L"facefxName", t, nameW);
        PBR::readStringByName(ref, L"facefxPath", t, pathW);
        ext->facefxName = wstrToUtf8(nameW.c_str());
        ext->facefxPath = wstrToUtf8(pathW.c_str());

        // Store on the IR node
        if (sn.nodeIndex >= 0 && sn.nodeIndex < static_cast<int32_t>(model.nodes.size())) {
            model.nodes[sn.nodeIndex].ext = std::move(ext);
        }
    }
}

} // namespace mdx_extract
