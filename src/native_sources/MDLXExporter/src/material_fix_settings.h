#pragma once

// The Material Fix settings: how a material that is not a Wc3 material becomes
// one. The Material Fix tool (post_startup_scripts/MaterialFix.ms) owns them
// and keeps them in plugcfg\WhiteoutDex_MaterialFix.ini, one profile for SD
// and one for HD; [General] FixProfile names the one Fix all converts with.
// Scene Monitor's built-in Fix all (scene_monitor_fixes.cpp, when the script
// is missing) and the exporter, for such materials it meets unconverted
// (wc3_material_extractor.cpp), read that same profile. Without the file they
// fall back to the old export dialog tab's MDLXExporter.ini [MaterialFix],
// the values the tool migrates on its first run.

#include <max.h>

#include <windows.h>

#include <string>

struct MaterialFixSettings {
    bool         unshaded      = false;
    bool         unfogged      = false;
    bool         twoSided      = false;
    bool         constantColor = false;
    bool         noDepthTest   = false;
    bool         noDepthSet    = false;  // Blend and above only, as the tool applies it
    bool         autoFilter    = false;  // FilterMode 0: Transparent with an opacity map, else None
    int          filterMode    = 1;      // 1=None, 2=Transparent, 3=Blend, 4=Add,
                                         // 5=Add2x, 6=Modulate, 7=Modulate2x
    std::wstring prefixPath;
    int          wrapMode      = 1;      // 1 keep the bitmap's, 2 U and V, 3 U, 4 V, 5 clamp
    bool         uTile         = false;  // wrapMode as flags, for the built-in Fix all
    bool         vTile         = false;
};

inline std::wstring plugcfgFile(const wchar_t* name) {
    Interface* gi = GetCOREInterface();
    MSTR dir;
    if (gi) dir = gi->GetDir(APP_PLUGCFG_DIR);
    return std::wstring(dir.data() ? dir.data() : L"") + L"\\" + name;
}

inline MaterialFixSettings loadMaterialFixSettings() {
    MaterialFixSettings s;
    wchar_t buf[512];

    // Disambiguate Win32 GetPrivateProfileStringW from MaxSDK::Util's
    // overload by taking a function pointer to the exact Win32 signature.
    // (Same trick the export dialog uses for the same conflict.)
    static auto Win32_GetPrivateProfileStringW =
        static_cast<DWORD(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR)>(
            &::GetPrivateProfileStringW);

    auto getStr = [&](const std::wstring& path, const wchar_t* sec, const wchar_t* key,
                      const wchar_t* def = L"") -> std::wstring {
        Win32_GetPrivateProfileStringW(sec, key, def, buf, 512, path.c_str());
        return buf;
    };
    auto isTrue = [](const std::wstring& v) { return v == L"true" || v == L"True" || v == L"1"; };

    const std::wstring tool = plugcfgFile(L"WhiteoutDex_MaterialFix.ini");
    if (GetFileAttributesW(tool.c_str()) != INVALID_FILE_ATTRIBUTES) {
        const wchar_t* sec = getStr(tool, L"General", L"FixProfile") == L"HD" ? L"HD" : L"SD";
        auto flag = [&](const wchar_t* key) { return isTrue(getStr(tool, sec, key)); };
        s.twoSided      = flag(L"TwoSided");
        s.unshaded      = flag(L"Unshaded");
        s.unfogged      = flag(L"Unfogged");
        s.constantColor = flag(L"ConstantColor");
        s.noDepthTest   = flag(L"NoDepthTest");
        s.noDepthSet    = flag(L"NoDepthSet");
        s.prefixPath    = getStr(tool, sec, L"PathPrefix");
        const int fm = _wtoi(getStr(tool, sec, L"FilterMode", L"0").c_str());
        s.autoFilter = (fm < 1 || fm > 7);
        s.filterMode = s.autoFilter ? 1 : fm;
        const int wm = _wtoi(getStr(tool, sec, L"WrapMode", L"1").c_str());
        s.wrapMode = (wm >= 1 && wm <= 5) ? wm : 1;
    } else {
        const std::wstring legacy = plugcfgFile(L"MDLXExporter.ini");
        auto flag = [&](const wchar_t* key) { return isTrue(getStr(legacy, L"MaterialFix", key)); };
        s.unshaded   = flag(L"Unshaded");
        s.unfogged   = flag(L"Unfogged");
        s.twoSided   = flag(L"Twosided");
        s.prefixPath = getStr(legacy, L"MaterialFix", L"PrefixPath");
        const int fm = _wtoi(getStr(legacy, L"MaterialFix", L"FilterMode", L"1").c_str());
        s.filterMode = (fm >= 1 && fm <= 7) ? fm : 1;
        const bool u = flag(L"UTile"), v = flag(L"VTile");
        s.wrapMode = (u && v) ? 2 : u ? 3 : v ? 4 : 1;
    }
    s.uTile = (s.wrapMode == 2 || s.wrapMode == 3);
    s.vTile = (s.wrapMode == 2 || s.wrapMode == 4);
    return s;
}
