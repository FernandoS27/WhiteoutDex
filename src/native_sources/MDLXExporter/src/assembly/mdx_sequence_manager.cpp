// MDLXExporter — Sequence manager implementation
#include "mdx_sequence_manager.h"

#include <icustattribcontainer.h>
#include <custattrib.h>
#include <iparamb2.h>
#include <inode.h>
#include <notetrck.h>
#include <cstdio>
#include <algorithm>
#include <unordered_map>
#include <cwctype>

namespace {

// ParamIDs by declaration order in the WhiteoutDexSequenceData custom attribute
enum SeqParamID : ParamID {
    PID_SeqNames = 0,
    PID_StartFrames = 1,
    PID_EndFrames = 2,
    PID_NonLooping = 3,
    PID_Rarity = 4,
    PID_MoveSpeed = 5,
    PID_SeqExtents = 6,
    PID_SharedGroup = 7,
};

IParamBlock2* findSequenceCA(INode* rootNode) {
    ICustAttribContainer* cac = rootNode->GetCustAttribContainer();
    if (!cac) return nullptr;

    for (int i = 0; i < cac->GetNumCustAttribs(); i++) {
        CustAttrib* ca = cac->GetCustAttrib(i);
        if (!ca) continue;

        // Get the first param block of each CA and check if it has the expected params
        IParamBlock2* pb = ca->GetParamBlock(0);
        if (!pb) continue;

        ParamBlockDesc2* desc = pb->GetDesc();
        if (!desc) continue;

        // Verify it's our CA by checking for seqNames param
        if (desc->Count() >= 7) {
            // Check that the first param is a string tab
            const ParamDef& pd = desc->GetParamDef(PID_SeqNames);
            if (pd.type == TYPE_STRING_TAB)
                return pb;
        }
    }
    return nullptr;
}

std::string wstrToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

void parseExtentString(const wchar_t* str, float& bound, Point3& mn, Point3& mx) {
    if (!str || !str[0]) return;
    swscanf_s(str, L"%f %f %f %f %f %f %f",
              &bound, &mn.x, &mn.y, &mn.z, &mx.x, &mx.y, &mx.z);
}

// ── Legacy Note Track fallback ──────────────────────────────

// Tokenize a note-key value string on quote, \r, \n, \t
std::vector<std::wstring> tokenizeNoteValue(const wchar_t* str) {
    std::vector<std::wstring> tokens;
    if (!str) return tokens;
    std::wstring cur;
    for (const wchar_t* p = str; *p; ++p) {
        if (*p == L'"' || *p == L'\r' || *p == L'\n' || *p == L'\t') {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur += *p;
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

bool wcsieq(const std::wstring& a, const wchar_t* b) {
    size_t blen = wcslen(b);
    if (a.size() != blen) return false;
    for (size_t i = 0; i < blen; i++)
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    return true;
}

bool wcsistartswith(const std::wstring& s, const wchar_t* prefix, size_t prefixLen) {
    if (s.size() < prefixLen) return false;
    for (size_t i = 0; i < prefixLen; i++)
        if (std::towlower(s[i]) != std::towlower(prefix[i])) return false;
    return true;
}

struct NoteKeyEntry {
    std::wstring value;
    TimeValue time;
};

std::vector<ir::Sequence> extractFromNoteTracks(INode* rootNode) {
    std::vector<ir::Sequence> sequences;

    if (!rootNode->HasNoteTracks()) return sequences;
    if (rootNode->NumNoteTracks() == 0) return sequences;

    auto* nt = dynamic_cast<DefNoteTrack*>(rootNode->GetNoteTrack(0));
    if (!nt) return sequences;

    int numKeys = nt->keys.Count();
    if (numKeys == 0) return sequences;

    // Group keys by their full value string (2 keys with identical value = 1 sequence)
    std::unordered_map<std::wstring, std::vector<NoteKeyEntry>> groups;
    std::vector<std::wstring> groupOrder;

    for (int i = 0; i < numKeys; i++) {
        NoteKey* nk = nt->keys[i];
        if (!nk) continue;
        const wchar_t* val = nk->note.data();
        if (!val || !val[0]) continue;

        std::wstring key(val);
        NoteKeyEntry entry;
        entry.value = key;
        entry.time = nk->time;

        if (groups.find(key) == groups.end())
            groupOrder.push_back(key);
        groups[key].push_back(entry);
    }

    // Build pairs from each group (sorted by time, take consecutive pairs)
    struct SeqParsed {
        std::string name;
        TimeValue startTime, endTime;
        bool nonLooping;
        float rarity, moveSpeed;
    };
    std::vector<SeqParsed> parsed;

    for (auto& keyStr : groupOrder) {
        auto& grp = groups[keyStr];
        if (grp.size() < 2) continue;

        std::sort(grp.begin(), grp.end(),
                  [](const NoteKeyEntry& a, const NoteKeyEntry& b) { return a.time < b.time; });

        size_t pairCount = grp.size() / 2;
        for (size_t p = 0; p < pairCount; p++) {
            auto& k1 = grp[p * 2];
            auto& k2 = grp[p * 2 + 1];

            auto tokens = tokenizeNoteValue(k1.value.c_str());
            std::string seqName = tokens.empty() ? "Unknown" : wstrToUtf8(tokens[0].c_str());
            bool nonLoop = false;
            float rare = 0.0f, speed = 0.0f;

            for (size_t t = 1; t < tokens.size(); t++) {
                if (wcsieq(tokens[t], L"NonLooping"))
                    nonLoop = true;
                else if (wcsistartswith(tokens[t], L"Rarity", 6))
                    rare = static_cast<float>(_wtof(tokens[t].c_str() + 6));
                else if (wcsistartswith(tokens[t], L"MoveSpeed", 9))
                    speed = static_cast<float>(_wtof(tokens[t].c_str() + 9));
            }

            SeqParsed sp;
            sp.name = seqName;
            sp.startTime = std::min(k1.time, k2.time);
            sp.endTime = std::max(k1.time, k2.time);
            sp.nonLooping = nonLoop;
            sp.rarity = rare;
            sp.moveSpeed = speed;
            parsed.push_back(std::move(sp));
        }
    }

    // Sort by start time
    std::sort(parsed.begin(), parsed.end(),
              [](const SeqParsed& a, const SeqParsed& b) { return a.startTime < b.startTime; });

    for (auto& sp : parsed) {
        ir::Sequence seq;
        seq.name = sp.name;
        seq.startTime = sp.startTime;
        seq.endTime = sp.endTime;
        seq.isLooping = !sp.nonLooping;
        seq.rarity = sp.rarity;
        seq.moveSpeed = sp.moveSpeed;
        sequences.push_back(std::move(seq));
    }

    return sequences;
}

} // anonymous namespace

std::vector<ir::Sequence> MdxSequenceManager::extractSequences(Interface* gi) {
    std::vector<ir::Sequence> sequences;

    INode* rootNode = gi->GetRootNode();
    if (!rootNode) return sequences;

    IParamBlock2* pb = findSequenceCA(rootNode);
    if (!pb) return extractFromNoteTracks(rootNode);

    int count = pb->Count(PID_SeqNames);
    sequences.reserve(count);

    int ticksPerFrame = GetTicksPerFrame();

    for (int i = 0; i < count; i++) {
        ir::Sequence seq;

        const MCHAR* name = nullptr;
        Interval valid = FOREVER;
        pb->GetValue(PID_SeqNames, 0, name, valid, i);
        seq.name = wstrToUtf8(name);

        int startFrame = 0, endFrame = 0;
        pb->GetValue(PID_StartFrames, 0, startFrame, valid, i);
        pb->GetValue(PID_EndFrames, 0, endFrame, valid, i);
        seq.startTime = startFrame * ticksPerFrame;
        seq.endTime = endFrame * ticksPerFrame;

        int nonLooping = 0;
        pb->GetValue(PID_NonLooping, 0, nonLooping, valid, i);
        seq.isLooping = (nonLooping == 0);

        pb->GetValue(PID_Rarity, 0, seq.rarity, valid, i);
        pb->GetValue(PID_MoveSpeed, 0, seq.moveSpeed, valid, i);

        const MCHAR* extStr = nullptr;
        pb->GetValue(PID_SeqExtents, 0, extStr, valid, i);
        parseExtentString(extStr, seq.extentRadius, seq.extentMin, seq.extentMax);

        sequences.push_back(std::move(seq));
    }

    return sequences;
}
