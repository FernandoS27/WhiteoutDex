// MDLXExporter — Wc3AttachPoint extractor implementation
#include "wc3_attachment_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>

namespace mdx_extract {

namespace {

// Attachment name prefixes — derived from Wc3AttachPoint.ms dropdown lists
const wchar_t* const kNameFirst[] = {
    L"", L"Bone", L"Ref", L"Sprite", L"Sprite",
    L"Overhead", L"Origin", L"Sprite"
};

const wchar_t* const kNameAdd1[] = {
    L"", L"First", L"Second", L"Third", L"Ground",
    L"Chest", L"Head", L"Weapon", L"Shield",
    L"Foot", L"Turret", L"Target", L"Mount",
    L"Smart", L"Sprite", L"Slam"
};

const wchar_t* const kNameAdd2[] = {
    L"", L"Left", L"Right", L"Middle", L"Rear",
    L"Ref"
};

std::string buildAttachmentName(int first, int add1, int add2) {
    std::wstring name;
    if (first > 0 && first < _countof(kNameFirst))
        name += kNameFirst[first];
    if (add1 > 0 && add1 < _countof(kNameAdd1)) {
        if (!name.empty()) name += L" ";
        name += kNameAdd1[add1];
    }
    if (add2 > 0 && add2 < _countof(kNameAdd2)) {
        if (!name.empty()) name += L" ";
        name += kNameAdd2[add2];
    }

    if (name.empty()) return "Attachment";

    int len = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "Attachment";
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, result.data(), len, nullptr, nullptr);
    return result;
}

} // anonymous namespace

void extractAttachments(const std::vector<core::SceneNode>& nodes,
                        ir::IRModel& model,
                        core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3AttachPoint") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        ir::Attachment attach;
        attach.nodeIndex = sn.nodeIndex;

        TimeValue t = 0;
        int nameFirst = 0, nameAdd1 = 0, nameAdd2 = 0;
        PBR::readIntByName(ref, L"nameFirst", t, nameFirst);
        PBR::readIntByName(ref, L"nameAdd1", t, nameAdd1);
        PBR::readIntByName(ref, L"nameAdd2", t, nameAdd2);

        attach.name = buildAttachmentName(nameFirst, nameAdd1, nameAdd2);

        // Attachment ID is derived from the composite name's index in a standard table
        // For export, the attachment ID is the index in the output attachments array
        attach.attachmentId = static_cast<int32_t>(model.attachments.size());

        model.attachments.push_back(std::move(attach));
    }
}

} // namespace mdx_extract
