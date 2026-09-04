// MDLXExporter — Wc3AttachPoint extractor implementation
//
// Reads attachment points (ATCH chunk) from the Max scene. Unlike
// Wc3Event, the scripted Wc3AttachPoint plugin uses name-based
// IParamBlock2 access reliably — no MaxScript route needed.
//
// Per handoff spec (exporter_handoff_event_attachment.md §2):
//
//   1. Name strategy: OPTION A — use the Max node name directly.
//      The builder picks up `ir::IRModel::Node::name` via buildNode(),
//      so we don't need to set ir::Attachment::name at all. The
//      node name was left as the reconstructed "Hand Right Ref"
//      style by the importer; the user may have edited it.
//
//   2. Read PB fields by NAME (not ID):
//        nameFirst, nameAdd1, nameAdd2       (ignored for Option A)
//        usesExternalModel, externalModelPath
//        usesAttachmentId, attachmentId
//        A_Visibility                         (animatable float)
//
//   3. externalModelPath was made absolute by the importer. Convert
//      back to a Wc3-relative path before writing to MDX.
//
//   4. Visibility track: use extractVisibilityTrack() from the
//      existing helper — already handles KATV conversion.

#include "wc3_attachment_extractor.h"
#include "../mdx_class_ids.h"
#include "visibility_track_helper.h"
#include <scene/paramblock_reader.h>
#include <iparamb2.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <fstream>
#include <windows.h>

namespace mdx_extract {

namespace {

// ── Attachment extraction debug log ──
std::ofstream& atchLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_attachment_debug.log";
        log.open(path, std::ios::trunc);
        log << "=== MDX Attachment Extraction Log ===\n\n";
    }
    return log;
}
#define ALOG_A  atchLog()
#define AFLUSH  atchLog().flush()

// Convert MSTR/wchar_t* → UTF-8 std::string.
std::string wideToUtf8(const wchar_t* w) {
    if (!w) return {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, w, -1,
                                     nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1,
                         out.data(), bytes, nullptr, nullptr);
    return out;
}

// Lowercase a wstring for case-insensitive path matching.
std::wstring toLowerW(const std::wstring& in) {
    std::wstring out = in;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    return out;
}

// Reverse-resolve an absolute model path back to a Wc3-relative form.
//
// The importer makes the path absolute (e.g.
// "C:\CASC\Textures\Effects\missile.mdx"). The MDX needs the
// Wc3-relative form ("Textures\Effects\missile.mdx").
//
// Strategy: scan for any of the well-known Wc3 root-folder markers
// in the path and cut from there. If nothing matches, assume the
// user typed a relative path directly and return it as-is.
std::string makeWc3RelativePath(const std::wstring& absPath) {
    if (absPath.empty()) return {};

    // If the path already looks relative (no drive letter and no
    // leading slash), return it unchanged.
    bool hasDrive = (absPath.size() >= 2 && absPath[1] == L':');
    bool hasLeadSlash = (!absPath.empty() &&
                         (absPath[0] == L'\\' || absPath[0] == L'/'));
    if (!hasDrive && !hasLeadSlash) {
        return wideToUtf8(absPath.c_str());
    }

    static const wchar_t* const kRootMarkers[] = {
        L"\\textures\\",
        L"\\units\\",
        L"\\buildings\\",
        L"\\doodads\\",
        L"\\abilities\\",
        L"\\ui\\",
        L"\\sound\\",
        L"\\replaceabletextures\\",
        L"\\environment\\",
        L"\\objects\\",
        L"\\effects\\",
        L"\\missiles\\",
        L"\\wc3mapoptimizer\\",
    };

    std::wstring lower = toLowerW(absPath);
    for (const wchar_t* marker : kRootMarkers) {
        size_t pos = lower.find(marker);
        if (pos != std::wstring::npos) {
            // Return from the character AFTER the leading slash
            std::wstring rel = absPath.substr(pos + 1);
            return wideToUtf8(rel.c_str());
        }
    }

    // Fallback: keep only the filename. Safer than writing an absolute
    // Windows path into the MDX.
    size_t lastSlash = absPath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        return wideToUtf8(absPath.substr(lastSlash + 1).c_str());
    }
    return wideToUtf8(absPath.c_str());
}

// Warcraft III addresses attachment points through the MDX `attachmentId`
// field, NOT through the node name. Verified against the Reforged debug build
// (Engine/Source/Model/MdlSgModel.cpp!ProcessAttachments):
//
//     attachIdToIndex.SetCount(attachments.back().attachmentId + 1);
//     memset(attachIdToIndex, 0xFF, bytes);
//     for (i = 0; i < count; ++i)
//         attachIdToIndex[attachments[i].attachmentId] = i;
//
// The name only picks a *slot* — ModelEnumLinkPoints hands the tokenizer
// `shared->attachId` as the index of the CLinkList property array, and
// ModelAddLink turns the matched slot back into an attachment through
// HasLinkPoint() -> attachIdToIndex[id].
//
// So the ids must satisfy two invariants or the model misbehaves in game:
//
//   * They must be unique. Duplicates collapse onto one slot and the LAST
//     attachment written wins it — which is why a rig whose attachments all
//     kept the plugin's default id of 0 puts every attached effect on its
//     final attachment (usually "Overhead Ref").
//   * The last attachment must carry the highest id, because that value alone
//     sizes the lookup table; anything above it is silently dropped.
//
// Blizzard's own exporter writes id == index. Re-derive that whenever the
// scene's ids don't already satisfy the invariants.
bool attachmentIdsAreValid(const std::vector<ir::Attachment>& atts) {
    if (atts.empty()) return true;

    std::vector<int32_t> seen;
    seen.reserve(atts.size());
    int32_t maxId = -1;

    for (const auto& a : atts) {
        if (a.attachmentId < 0) return false;
        if (std::find(seen.begin(), seen.end(), a.attachmentId) != seen.end())
            return false;
        seen.push_back(a.attachmentId);
        if (a.attachmentId > maxId) maxId = a.attachmentId;
    }

    return atts.back().attachmentId == maxId;
}

} // anonymous namespace

void extractAttachments(const std::vector<core::SceneNode>& nodes,
                        ir::IRModel& model,
                        core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    ALOG_A << "── extractAttachments ──\n\n";

    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3AttachPoint") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) {
            ALOG_A << "  [SKIP] node has no ReferenceTarget\n";
            continue;
        }

        ir::Attachment attach;
        attach.nodeIndex = sn.nodeIndex;

        // ── Name: OPTION A ──
        // The builder reads ir::IRModel::Node::name via buildNode() and
        // uses it directly for the MDX node name. We leave
        // ir::Attachment::name empty intentionally — setting it here
        // wouldn't propagate because builder ignores it for attachments
        // (see mdx_model_builder.cpp §10).
        std::string rawName;
        if (const MCHAR* nm = sn.maxNode->GetName()) {
            rawName = wideToUtf8(nm);
        }
        attach.name = rawName;  // stored for completeness / logging

        ALOG_A << "  Attachment node[" << sn.nodeIndex
               << "] name='" << rawName << "'\n";

        // ── Read PB fields by NAME ──
        TimeValue t = 0;

        BOOL usesExternalModel = FALSE;
        PBR::readBoolByName(ref, L"usesExternalModel", t, usesExternalModel);

        std::wstring extPath;
        PBR::readStringByName(ref, L"externalModelPath", t, extPath);

        BOOL usesAttachmentId = FALSE;
        PBR::readBoolByName(ref, L"usesAttachmentId", t, usesAttachmentId);

        int attachmentId = 0;
        PBR::readIntByName(ref, L"attachmentId", t, attachmentId);

        // ── Path resolution ──
        // Only write a path when the user explicitly opted in via
        // usesExternalModel. Otherwise the MDX gets 260 zero bytes
        // (handled downstream by the writer).
        if (usesExternalModel == TRUE && !extPath.empty()) {
            attach.path = makeWc3RelativePath(extPath);
        } else {
            attach.path.clear();
        }

        // ── Attachment ID ──
        // Take the stored value even when usesAttachmentId=false, so a
        // round-tripped model keeps the IDs the importer read out of the
        // file. The set is validated (and renumbered when it can't work in
        // game) after every attachment has been collected — see
        // attachmentIdsAreValid().
        attach.attachmentId = static_cast<int32_t>(attachmentId);

        // ── Visibility track (KATV) ──
        // The helper checks for animated keys; static 1.0 returns -1
        // so the builder writes no KATV chunk.
        attach.visibilityTrackIndex = extractVisibilityTrack(sn.maxNode, model);

        // ── Debug log ──
        ALOG_A << "    usesExternalModel=" << (usesExternalModel ? "true" : "false")
               << " path='" << attach.path << "'\n";
        ALOG_A << "    usesAttachmentId=" << (usesAttachmentId ? "true" : "false")
               << " attachmentId=" << attach.attachmentId << "\n";
        ALOG_A << "    visibilityTrackIndex=" << attach.visibilityTrackIndex << "\n";
        AFLUSH;

        model.attachments.push_back(std::move(attach));
    }

    // ── Attachment ID invariants ──
    // A scene authored in Max leaves every attachment on the plugin's default
    // ID of 0, which makes Warcraft III resolve every attach point to the last
    // attachment in the file. Renumber whenever the set can't work in game.
    if (!attachmentIdsAreValid(model.attachments)) {
        for (size_t i = 0; i < model.attachments.size(); ++i)
            model.attachments[i].attachmentId = static_cast<int32_t>(i);

        reporter.info(L"Attachment IDs were duplicated or out of order and have "
                      L"been renumbered 0-N in export order. Warcraft III keys "
                      L"attachment points by ID, so the original numbering would "
                      L"have collapsed every attach point onto one attachment.");
        ALOG_A << "  [FIX] attachment IDs renumbered sequentially 0.."
               << (model.attachments.empty() ? 0 : model.attachments.size() - 1)
               << "\n";
    }

    ALOG_A << "\n  Total attachments extracted: " << model.attachments.size() << "\n";
    for (size_t i = 0; i < model.attachments.size(); ++i) {
        ALOG_A << "    [" << i << "] '" << model.attachments[i].name
               << "' attachmentId=" << model.attachments[i].attachmentId << "\n";
    }
    AFLUSH;
}

} // namespace mdx_extract
