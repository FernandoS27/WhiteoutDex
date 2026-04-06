// MaxCore — ParamBlockReader implementation
// Based on the pattern from WhiteoutDexRenderer/src/extract.cpp
#include "paramblock_reader.h"
#include <iparamb2.h>
#include <ref.h>
#include <string>

namespace core {

IParamBlock2* ParamBlockReader::findParamBlock(ReferenceTarget* target, int pbIndex) {
    if (!target) return nullptr;
    int count = 0;
    for (int i = 0; i < target->NumRefs(); ++i) {
        ReferenceTarget* ref = target->GetReference(i);
        if (!ref) continue;
        IParamBlock2* pb = dynamic_cast<IParamBlock2*>(ref);
        if (pb) {
            if (count == pbIndex) return pb;
            ++count;
        }
    }
    return nullptr;
}

// Helper to get a C-string pointer from ParamBlockDesc2::int_name.
// In Max 2016, int_name is a raw MCHAR*. In newer SDKs, it's a WStr.
// Both implicitly convert to const wchar_t*, but WStr deletes operator bool().
static const wchar_t* getIntNameCStr(const ParamBlockDesc2* desc) {
    const wchar_t* s = desc->int_name;
    return s;
}

IParamBlock2* ParamBlockReader::findParamBlockByName(ReferenceTarget* target, const wchar_t* name) {
    if (!target || !name) return nullptr;
    for (int i = 0; i < target->NumRefs(); ++i) {
        ReferenceTarget* ref = target->GetReference(i);
        if (!ref) continue;
        IParamBlock2* pb = dynamic_cast<IParamBlock2*>(ref);
        if (pb) {
            ParamBlockDesc2* desc = pb->GetDesc();
            const wchar_t* iname = desc ? getIntNameCStr(desc) : nullptr;
            if (iname && iname[0] != L'\0' && _wcsicmp(iname, name) == 0)
                return pb;
        }
    }
    return nullptr;
}

int ParamBlockReader::readInt(IParamBlock2* pb, ParamID id, TimeValue t, int fallback) {
    if (!pb) return fallback;
    int val = fallback;
    Interval iv;
    if (pb->GetValue(id, t, val, iv) != 0) return val;
    return fallback;
}

float ParamBlockReader::readFloat(IParamBlock2* pb, ParamID id, TimeValue t, float fallback) {
    if (!pb) return fallback;
    float val = fallback;
    Interval iv;
    if (pb->GetValue(id, t, val, iv) != 0) return val;
    return fallback;
}

BOOL ParamBlockReader::readBool(IParamBlock2* pb, ParamID id, TimeValue t, BOOL fallback) {
    if (!pb) return fallback;
    BOOL val = fallback;
    Interval iv;
    if (pb->GetValue(id, t, val, iv) != 0) return val;
    return fallback;
}

Point3 ParamBlockReader::readPoint3(IParamBlock2* pb, ParamID id, TimeValue t, Point3 fallback) {
    if (!pb) return fallback;
    Point3 val = fallback;
    Interval iv;
    if (pb->GetValue(id, t, val, iv) != 0) return val;
    return fallback;
}

Color ParamBlockReader::readColor(IParamBlock2* pb, ParamID id, TimeValue t, Color fallback) {
    if (!pb) return fallback;
    Color val = fallback;
    Interval iv;
    if (pb->GetValue(id, t, val, iv) != 0) return val;
    return fallback;
}

std::wstring ParamBlockReader::readString(IParamBlock2* pb, ParamID id, TimeValue t) {
    if (!pb) return L"";
    const MCHAR* val = nullptr;
    Interval iv;
    if (pb->GetValue(id, t, val, iv) != 0 && val)
        return std::wstring(val);
    return L"";
}

Texmap* ParamBlockReader::readTexmap(IParamBlock2* pb, ParamID id, TimeValue t) {
    if (!pb) return nullptr;
    Texmap* val = nullptr;
    Interval iv;
    pb->GetValue(id, t, val, iv);
    return val;
}

// Name-based lookup helpers — iterate all param blocks on a ReferenceTarget
static IParamBlock2* findPBWithParam(ReferenceTarget* target, const wchar_t* name, ParamID& outID) {
    if (!target) return nullptr;
    for (int i = 0; i < target->NumRefs(); ++i) {
        ReferenceTarget* ref = target->GetReference(i);
        if (!ref) continue;
        IParamBlock2* pb = dynamic_cast<IParamBlock2*>(ref);
        if (!pb) continue;
        ParamBlockDesc2* desc = pb->GetDesc();
        if (!desc) continue;
        for (int p = 0; p < desc->count; ++p) {
            const ParamDef& pd = desc->paramdefs[p];
            if (pd.int_name && _wcsicmp(pd.int_name, name) == 0) {
                outID = pd.ID;
                return pb;
            }
        }
    }
    return nullptr;
}

bool ParamBlockReader::readFloatByName(ReferenceTarget* target, const wchar_t* name, TimeValue t, float& out) {
    ParamID id;
    IParamBlock2* pb = findPBWithParam(target, name, id);
    if (!pb) return false;
    Interval iv;
    return pb->GetValue(id, t, out, iv) != 0;
}

bool ParamBlockReader::readIntByName(ReferenceTarget* target, const wchar_t* name, TimeValue t, int& out) {
    ParamID id;
    IParamBlock2* pb = findPBWithParam(target, name, id);
    if (!pb) return false;
    Interval iv;
    return pb->GetValue(id, t, out, iv) != 0;
}

bool ParamBlockReader::readBoolByName(ReferenceTarget* target, const wchar_t* name, TimeValue t, BOOL& out) {
    ParamID id;
    IParamBlock2* pb = findPBWithParam(target, name, id);
    if (!pb) return false;
    Interval iv;
    return pb->GetValue(id, t, out, iv) != 0;
}

bool ParamBlockReader::readColorByName(ReferenceTarget* target, const wchar_t* name, TimeValue t, Color& out) {
    ParamID id;
    IParamBlock2* pb = findPBWithParam(target, name, id);
    if (!pb) return false;
    Interval iv;
    return pb->GetValue(id, t, out, iv) != 0;
}

bool ParamBlockReader::readTexmapByName(ReferenceTarget* target, const wchar_t* name, Texmap*& out) {
    ParamID id;
    IParamBlock2* pb = findPBWithParam(target, name, id);
    if (!pb) return false;
    Interval iv;
    return pb->GetValue(id, 0, out, iv) != 0;
}

bool ParamBlockReader::readStringByName(ReferenceTarget* target, const wchar_t* name, TimeValue t, std::wstring& out) {
    ParamID id;
    IParamBlock2* pb = findPBWithParam(target, name, id);
    if (!pb) return false;
    const MCHAR* str = nullptr;
    Interval iv;
    if (pb->GetValue(id, t, str, iv) && str) {
        out = str;
        return true;
    }
    return false;
}

} // namespace core
