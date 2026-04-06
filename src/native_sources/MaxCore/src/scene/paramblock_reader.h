// MaxCore — Safe IParamBlock2 accessor
#pragma once

#include <max.h>
#include <iparamb2.h>
#include <inode.h>
#include <maxtypes.h>
#include <string>

namespace core {

class ParamBlockReader {
public:
    // Find a param block by ordinal index on a ReferenceTarget
    static IParamBlock2* findParamBlock(ReferenceTarget* target, int pbIndex);

    // Find a param block by its internal name
    static IParamBlock2* findParamBlockByName(ReferenceTarget* target, const wchar_t* name);

    // Typed read with fallback
    static int readInt(IParamBlock2* pb, ParamID id, TimeValue t, int fallback = 0);
    static float readFloat(IParamBlock2* pb, ParamID id, TimeValue t, float fallback = 0.0f);
    static BOOL readBool(IParamBlock2* pb, ParamID id, TimeValue t, BOOL fallback = FALSE);
    static Point3 readPoint3(IParamBlock2* pb, ParamID id, TimeValue t, Point3 fallback = Point3(0, 0, 0));
    static Color readColor(IParamBlock2* pb, ParamID id, TimeValue t, Color fallback = Color(0, 0, 0));
    static std::wstring readString(IParamBlock2* pb, ParamID id, TimeValue t);
    static Texmap* readTexmap(IParamBlock2* pb, ParamID id, TimeValue t);

    // Name-based lookup (iterates params to find by internal name)
    static bool readFloatByName(ReferenceTarget* target, const wchar_t* name, TimeValue t, float& out);
    static bool readIntByName(ReferenceTarget* target, const wchar_t* name, TimeValue t, int& out);
    static bool readBoolByName(ReferenceTarget* target, const wchar_t* name, TimeValue t, BOOL& out);
    static bool readColorByName(ReferenceTarget* target, const wchar_t* name, TimeValue t, Color& out);
    static bool readTexmapByName(ReferenceTarget* target, const wchar_t* name, Texmap*& out);
    static bool readStringByName(ReferenceTarget* target, const wchar_t* name, TimeValue t, std::wstring& out);
};

} // namespace core
