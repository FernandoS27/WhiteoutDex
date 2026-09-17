// MDLXExporter — Wc3Event extractor implementation
//
// Reads event objects (EVTS chunk) from the Max scene. Events are
// simpleManipulator scripted plugins, so several things behave
// differently from normal plugins:
//
//   1. Node name has "Obj:" prefix (added by importer for UI parsing) —
//      must be stripped before writing to MDX.
//   2. keyList is an IntTab parameter with DYNAMIC ParamID (scripted
//      plugins don't expose stable numeric IDs). Direct IParamBlock2
//      access is unreliable — use MaxScript route via
//      ExecuteMAXScriptScript instead.
//   3. Times in keyList are stored as FRAMES (not ticks, not ms).
//      We convert frames → ticks here; the builder does ticks → ms.
//
// See exporter_handoff_event_attachment.md for the full spec.

#include "wc3_event_extractor.h"
#include "../mdx_class_ids.h"
#include <algorithm>
#include <scene/paramblock_reader.h>
#include <animation/global_sequence_helper.h>
#include <iparamb2.h>
#include <maxscript/maxscript.h>
#include <maxscript/util/listener.h>
#include <sstream>
#include <string>
#include <fstream>
#include <algorithm>
#include <windows.h>

namespace mdx_extract {

namespace {

// ── Event extraction debug log ──
// Writes to %TEMP%\mdlx_event_debug.log — helpful for round-trip
// verification and for diagnosing cases where keyList can't be read.
std::ofstream& evtLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_event_debug.log";
        log.open(path, std::ios::trunc);
        log << "=== MDX Event Extraction Log ===\n\n";
    }
    return log;
}
#define ELOG  evtLog()
#define EFLUSH evtLog().flush()

// Strip "Obj:" prefix (added by the importer for UI parsing).
// The prefix is never part of the actual MDX event code.
std::string stripObjPrefix(const std::string& name) {
    if (name.size() >= 4 && name.compare(0, 4, "Obj:") == 0) {
        return name.substr(4);
    }
    return name;
}

// Convert a wide MSTR/wchar_t* string to UTF-8 std::string.
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

// Escape backslashes and quotes for embedding into a MaxScript string literal.
std::wstring escapeForMaxScript(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size() + 8);
    for (wchar_t c : in) {
        if (c == L'\\' || c == L'\"') {
            out.push_back(L'\\');
        }
        out.push_back(c);
    }
    return out;
}

// Read the keyList (frames) from an event object via MaxScript route.
//
// Direct IParamBlock2 access doesn't work reliably for scripted
// simpleManipulator plugins because the ParamID assignment is dynamic.
// Instead we invoke MaxScript to enumerate node.keyList by name and
// return a comma-separated string we can parse in C++.
//
// Returns frame numbers (0-based Max frame index).
std::vector<int> readEventKeyList(INode* node) {
    std::vector<int> frames;
    if (!node) return frames;

    const MCHAR* nameRaw = node->GetName();
    if (!nameRaw) return frames;

    std::wstring nodeName(nameRaw);

    // Build the MaxScript (node resolved by HANDLE — names are not unique,
    // and getNodeByName would read the keyList off the wrong node):
    //   (local n = maxOps.getNodeByHandle <handle>;
    //    local result = "";
    //    if n != undefined and isProperty n #keyList do (
    //        for i = 1 to n.keyList.count do (
    //            if i > 1 do result += ",";
    //            result += (n.keyList[i] as string)
    //        )
    //    );
    //    result)
    std::wstringstream ss;
    ss << L"(local n = maxOps.getNodeByHandle " << node->GetHandle() << L";"
       << L" local result = \"\";"
       << L" if n != undefined and isProperty n #keyList do ("
       << L"   for i = 1 to n.keyList.count do ("
       << L"     if i > 1 do result += \",\";"
       << L"     result += (n.keyList[i] as string)"
       << L"   )"
       << L" );"
       << L" result)";

    std::wstring script = ss.str();

    FPValue result;
    result.type = TYPE_VOID;
    BOOL ok = FALSE;
    try {
        ok = ExecuteMAXScriptScript(
            const_cast<wchar_t*>(script.c_str()),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
            MAXScript::ScriptSource::NonEmbedded,
#endif
            TRUE,   // quietErrors: don't pop a listener window on error
            &result);
    } catch (...) {
        ok = FALSE;
    }

    if (!ok) {
        ELOG << "  [WARN] ExecuteMAXScriptScript failed for node '"
             << wideToUtf8(nodeName.c_str()) << "'\n";
        return frames;
    }

    if (result.type != TYPE_STRING || !result.s) {
        ELOG << "  [WARN] MaxScript returned non-string for node '"
             << wideToUtf8(nodeName.c_str())
             << "' (type=" << static_cast<int>(result.type) << ")\n";
        return frames;
    }

    std::wstring str(result.s);
    if (str.empty()) return frames;

    // Parse comma-separated integer list
    std::wstringstream iss(str);
    std::wstring token;
    while (std::getline(iss, token, L',')) {
        // Trim whitespace
        while (!token.empty() && (token.front() == L' ' || token.front() == L'\t'))
            token.erase(token.begin());
        while (!token.empty() && (token.back() == L' ' || token.back() == L'\t'))
            token.pop_back();
        if (token.empty()) continue;
        try {
            int frame = std::stoi(token);
            frames.push_back(frame);
        } catch (...) {
            // skip malformed token
        }
    }

    return frames;
}

} // anonymous namespace

void extractEvents(const std::vector<core::SceneNode>& nodes,
                   ir::IRModel& model,
                   core::ExportErrorReporter& reporter)
{
    ELOG << "── extractEvents ──\n";
    int tpf = GetTicksPerFrame();
    ELOG << "  ticksPerFrame=" << tpf << "\n\n";

    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3Event") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) {
            ELOG << "  [SKIP] node has no ReferenceTarget\n";
            continue;
        }

        ir::EventObject evt;
        evt.nodeIndex = sn.nodeIndex;

        // ── Node name → eventCode (strip "Obj:" prefix) ──
        std::string rawName;
        const MCHAR* nodeName = sn.maxNode->GetName();
        if (nodeName) {
            rawName = wideToUtf8(nodeName);
        }
        std::string eventCode = stripObjPrefix(rawName);
        evt.eventCode = eventCode;

        ELOG << "  Event node[" << sn.nodeIndex << "] '" << rawName
             << "' → eventCode='" << eventCode << "'\n";

        // ── Read keyList via MaxScript route ──
        std::vector<int> frames = readEventKeyList(sn.maxNode);

        ELOG << "    keyList frames (count=" << frames.size() << "):";
        for (int f : frames) ELOG << " " << f;
        ELOG << "\n";

        // Sort defensively — UI always inserts sorted, but a user could
        // theoretically have unordered entries.
        std::sort(frames.begin(), frames.end());

        // Convert frames → ticks; builder converts ticks → ms.
        for (int f : frames) {
            TimeValue ticks = static_cast<TimeValue>(f) * tpf;
            evt.keyTimes.push_back(ticks);
        }

        // KEVT global sequence: the importer keeps its duration (ms) in a
        // UserProp, because Wc3RefEvent's duplicate parameter blocks make a
        // new parameter unsafe (see reference memory on scripted plugins).
        int gsMs = 0;
        if (sn.maxNode->GetUserPropInt(_T("Wc3GlobalSequence"), gsMs) && gsMs > 0) {
            const TimeValue gsTicks = static_cast<TimeValue>(
                (static_cast<int64_t>(gsMs) * 4800 + 500) / 1000);
            evt.globalSequenceIndex = core::anim::registerGlobalSequence(model, gsTicks);
            ELOG << "    global sequence " << gsMs << " ms -> idx "
                 << evt.globalSequenceIndex << "\n";
        }

        EFLUSH;
        model.eventObjects.push_back(std::move(evt));
    }

    ELOG << "\n  Total events extracted: " << model.eventObjects.size() << "\n";
    EFLUSH;
}

} // namespace mdx_extract
