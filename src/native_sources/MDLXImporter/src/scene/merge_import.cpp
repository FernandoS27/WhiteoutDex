// MDLXImporter — Merge import (see merge_import.h).
#include "merge_import.h"
#include "../mdlx_class_ids.h"

#include <maxscript/maxscript.h>
#include <modstack.h>   // IDerivedObject

#include <algorithm>
#include <cwctype>
#include <functional>
#include <sstream>

namespace mdx_merge {

namespace {

BOOL runScript(const std::wstring& script, FPValue* result) {
    try {
        return ExecuteMAXScriptScript(const_cast<wchar_t*>(script.c_str()),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
                                      MAXScript::ScriptSource::NonEmbedded,
#endif
                                      TRUE, result);
    } catch (...) {
        return FALSE;
    }
}

std::wstring lower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

bool isTaken(const std::vector<std::wstring>& taken, const std::wstring& name) {
    const std::wstring l = lower(name);
    return std::any_of(taken.begin(), taken.end(), [&](const std::wstring& t) { return lower(t) == l; });
}

/// Splits a trailing variant number off: "Stand - 2" -> {"Stand", " - ", 2},
/// "Attack 1" -> {"Attack", " ", 1}. The number must follow a space or a dash;
/// without one, `number` is -1 and `base` is the whole name.
struct Variant {
    std::wstring base, separator;
    int number = -1;
};

Variant splitVariant(const std::wstring& name) {
    Variant v{name, L"", -1};
    size_t digits = name.size();
    while (digits > 0 && std::iswdigit(name[digits - 1])) --digits;
    if (digits == name.size() || digits == 0 || name.size() - digits > 6) return v;
    size_t sep = digits;
    while (sep > 0 && (name[sep - 1] == L' ' || name[sep - 1] == L'-')) --sep;
    if (sep == digits || sep == 0) return v;
    v.base = name.substr(0, sep);
    v.separator = name.substr(sep, digits - sep);
    v.number = std::stoi(name.substr(digits));
    return v;
}

/// `words` (one or more, space separated) appear in `name` as whole words.
bool hasWords(const std::wstring& name, const std::wstring& words) {
    const std::wstring n = L" " + lower(name) + L" ", w = L" " + lower(words) + L" ";
    return n.find(w) != std::wstring::npos;
}

/// A node whose visibility the game shows: a mesh (not a bone or a target), an
/// emitter, ribbon, popcorn, light or attachment point.
bool isVisibleObject(INode* n) {
    Object* obj = n->GetObjectRef();
    while (obj && (obj->SuperClassID() == GEN_DERIVOB_CLASS_ID || obj->SuperClassID() == WSM_DERIVOB_CLASS_ID))
        obj = static_cast<IDerivedObject*>(obj)->GetObjRef();
    if (!obj) return false;
    const Class_ID cid = obj->ClassID();
    if (cid == mdx_ids::WC3_ATTACH_POINT || cid == mdx_ids::WC3_LIGHT || cid == mdx_ids::WC3_PARTICLES1 ||
        cid == mdx_ids::WC3_PARTICLES2 || cid == mdx_ids::WC3_RIBBON || cid == mdx_ids::BLIZZ_POPCORN)
        return true;
    if (cid == BONE_OBJ_CLASSID || n->IsTarget()) return false;
    const SClass_ID scid = obj->SuperClassID();
    return scid == GEOMOBJECT_CLASS_ID || scid == LIGHT_CLASS_ID;
}

void appendTimes(std::wostringstream& ss, const std::vector<Interval>& ranges) {
    ss << L"#(";
    bool first = true;
    for (const Interval& r : ranges) {
        ss << (first ? L"" : L", ") << r.Start() << L"t, " << r.End() << L"t";
        first = false;
    }
    ss << L")";
}

} // namespace

std::vector<SceneSequence> readSceneSequences() {
    std::vector<SceneSequence> out;
    FPValue result;
    const std::wstring script =
        L"(local r = \"\"\n"
        L"try (\n"
        L"  ::WdxSequenceStorage.ensureCA()\n"
        L"  for i = 1 to rootNode.seqNames.count do\n"
        L"    r += rootNode.seqNames[i] + \"\\t\" + (rootNode.startFrames[i] as string) + \"\\t\" + "
        L"(rootNode.endFrames[i] as string) + \"\\n\"\n"
        L") catch (r = \"\")\n"
        L"r)";
    if (!runScript(script, &result) || result.type != TYPE_STRING || !result.s) return out;

    const TimeValue tpf = GetTicksPerFrame();
    std::wistringstream lines{std::wstring(result.s)};
    std::wstring line;
    while (std::getline(lines, line)) {
        const size_t a = line.find(L'\t'), b = a == std::wstring::npos ? a : line.find(L'\t', a + 1);
        if (b == std::wstring::npos) continue;
        try {
            SceneSequence s;
            s.name = line.substr(0, a);
            s.start = std::stoi(line.substr(a + 1, b - a - 1)) * tpf;
            s.end = std::stoi(line.substr(b + 1)) * tpf;
            out.push_back(std::move(s));
        } catch (...) {
        }
    }
    return out;
}

const std::vector<std::wstring>& nameTags() {
    static const std::vector<std::wstring> tags = {
        L"",           L"Alternate",      L"Gold",           L"Lumber",          L"Work",
        L"Defend",     L"Swim",           L"Upgrade First",  L"Upgrade Second",  L"Upgrade Third",
        L"Upgrade Fourth", L"Upgrade Fifth",
    };
    return tags;
}

std::wstring tagSequenceName(const std::wstring& name, const std::wstring& tag,
                             std::vector<std::wstring>& taken) {
    std::wstring tagged = name;
    if (!tag.empty() && !hasWords(name, tag)) {
        const Variant v = splitVariant(name);
        tagged = v.number >= 0 ? v.base + L" " + tag + v.separator + std::to_wstring(v.number)
                               : name + L" " + tag;
    }

    std::wstring result = tagged;
    if (isTaken(taken, result)) {
        const Variant v = splitVariant(tagged);
        const std::wstring base = v.number >= 0 ? v.base : tagged;
        const std::wstring sep = v.number >= 0 ? v.separator : std::wstring(L" - ");
        for (int n = std::max(2, v.number + 1); isTaken(taken, result); ++n)
            result = base + sep + std::to_wstring(n);
    }
    taken.push_back(result);
    return result;
}

std::set<ULONG> sceneNodeHandles(Interface* gi) {
    std::set<ULONG> handles;
    if (!gi || !gi->GetRootNode()) return handles;
    std::function<void(INode*)> walk = [&](INode* n) {
        for (int i = 0; i < n->NumberOfChildren(); ++i) {
            INode* c = n->GetChildNode(i);
            handles.insert(c->GetHandle());
            walk(c);
        }
    };
    walk(gi->GetRootNode());
    return handles;
}

void hideDuringOtherSequences(Interface* gi, const std::set<ULONG>& oldHandles,
                              const std::vector<Interval>& oldSequences,
                              const std::vector<Interval>& newSequences) {
    if (!gi || !gi->GetRootNode() || oldSequences.empty() || newSequences.empty()) return;

    // One MAXScript call for all nodes. Per node: the visibility at its own
    // sequence boundaries is read first and written back as keys afterwards,
    // so the 0 keys of the other model's sequences cannot change what its own
    // sequences show (a Boolean track holds the 0 until the next key, a
    // linear or Bezier one would interpolate towards it). addNewKey returns
    // the key already at that time instead of adding a second one.
    std::wostringstream ss;
    ss << L"(\n"
          L"fn wdxMergeHide h hideTimes ownTimes = (\n"
          L"  local n = maxOps.getNodeByHandle h\n"
          L"  if n == undefined do return false\n"
          L"  local c = getVisController n\n"
          L"  if c == undefined do (\n"
          L"    n.visibility = bezier_float()\n"
          L"    n.visibility.controller = boolean_float()\n"
          L"    c = getVisController n\n"
          L"    local k0 = addNewKey c 0f\n"
          L"    k0.value = 1.0\n"
          L"  )\n"
          L"  local own = for t in ownTimes collect (at time t c.value)\n"
          L"  local bez = classOf c == bezier_float\n"
          L"  for t in hideTimes do (\n"
          L"    local k = addNewKey c t\n"
          L"    k.value = 0.0\n"
          L"    if bez do (k.inTangentType = #step; k.outTangentType = #step)\n"
          L"  )\n"
          L"  for i = 1 to ownTimes.count do (\n"
          L"    local k = addNewKey c ownTimes[i]\n"
          L"    k.value = own[i]\n"
          L"  )\n"
          L"  true\n"
          L")\n";

    int count = 0;
    std::function<void(INode*)> walk = [&](INode* n) {
        for (int i = 0; i < n->NumberOfChildren(); ++i) {
            INode* c = n->GetChildNode(i);
            if (isVisibleObject(c)) {
                const bool isOld = oldHandles.count(c->GetHandle()) > 0;
                ss << L"try (wdxMergeHide " << c->GetHandle() << L" ";
                appendTimes(ss, isOld ? newSequences : oldSequences);
                ss << L" ";
                appendTimes(ss, isOld ? oldSequences : newSequences);
                ss << L") catch ()\n";
                ++count;
            }
            walk(c);
        }
    };
    walk(gi->GetRootNode());
    ss << L"ok)";
    if (count > 0) runScript(ss.str(), nullptr);
}

} // namespace mdx_merge
