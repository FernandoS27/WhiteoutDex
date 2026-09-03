#pragma once

// ============================================================================
// The user-editable MPQ load order.
//
// One list, edited in WhiteoutDex Settings ("MPQ Archives -> Edit Load
// Order..."), read by everything in the toolkit that opens an archive: the
// renderer's two content providers, the importer's texture resolver, the asset
// browser's tree and its extractor. Before this each of those had its own
// order baked in - three different ones - and a custom archive was reachable
// from none of them.
//
//   <plugcfg>\WhiteoutDex_Settings.ini
//   [MPQ]
//   List=War3Patch.mpq|War3xLocal.mpq|War3x.mpq|War3.mpq|D:\Mods\Custom.mpq
//
// Order IS priority: the first archive holding a path is the one that answers,
// which is how a patch overrides the base game it shipped to patch. '|' is the
// separator because it is illegal in a Windows filename, so no entry can
// contain one; it is also what WhiteoutFlakes' own [IO] MpqList uses, so the
// two files stay readable to the same eyes.
//
// An entry is either a name relative to the configured MPQ directory
// ("War3Patch.mpq" - what a stock install wants, and what survives the user
// moving the game) or a full path ("D:\Mods\Custom.mpq" - what makes a custom
// archive possible at all). Resolve() tells them apart the way
// std::filesystem does, so an absolute entry simply replaces the base.
//
// A MISSING key is not an empty list. It means the user never opened the
// editor, and every caller then keeps whatever default it had - which is what
// lets a later change to a default order still reach those users. Callers
// distinguish the two by an empty return from LoadList().
//
// Header-only on purpose: the two DLLs that read this share no static library
// (MaxCore is linked by the importer and exporter, not by the renderer), and a
// setting this small is not worth a fourth build target.
// ============================================================================

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace wdx::mpq {

// The retail Warcraft III archives, highest priority first. War3Patch holds
// whatever the last patch replaced, so it has to precede the base archives it
// overlays; Deprecated is last because it is exactly the content the game
// stopped using.
//
// Longer than WhiteoutFlakes' DefaultArchives(Wc3), which names only the three
// an English Reign of Chaos install has. The two locale archives and
// Deprecated.mpq are present on a Frozen Throne install and hold content
// nothing else does, so leaving them out of the default the user is shown
// would make the editor's "Reset" a downgrade.
inline std::vector<std::wstring> DefaultNames() {
    return {L"War3Patch.mpq", L"War3xLocal.mpq", L"War3Local.mpq",
            L"War3x.mpq",     L"War3.mpq",       L"Deprecated.mpq"};
}

namespace detail {

// The Win32 GetPrivateProfileStringW, named unambiguously.
//
// The 2026 Max SDK added a MaxSDK::Util overload that is visible here through
// the SDK headers the callers include first, so a plain call is ambiguous.
// Taking a pointer to the exact Win32 signature picks the right one; the
// exporter, the importer and dllmain.cpp all use the same trick for the same
// conflict.
inline std::wstring IniString(const std::wstring& iniPath, const wchar_t* section,
                              const wchar_t* key) {
    if (iniPath.empty() || GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return {};

    static auto Win32_GetPrivateProfileStringW =
        static_cast<DWORD(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR)>(
            &::GetPrivateProfileStringW);

    // A load order of a dozen full paths runs past any fixed small buffer, and
    // the API's only "too small" signal is returning nSize-1. Grow once and
    // give up after that - 128 KB is more archives than a person will list.
    for (DWORD cap = 4096; cap <= 65536; cap *= 16) {
        std::vector<wchar_t> buf(cap, L'\0');
        const DWORD n =
            Win32_GetPrivateProfileStringW(section, key, L"", buf.data(), cap, iniPath.c_str());
        if (n < cap - 1)
            return std::wstring(buf.data(), n);
    }
    return {};
}

inline void TrimInPlace(std::wstring& s) {
    const auto notSpace = [](wchar_t c) { return c != L' ' && c != L'\t'; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
}

} // namespace detail

inline std::wstring SettingsIniPath(const std::wstring& plugcfgDir) {
    if (plugcfgDir.empty())
        return {};
    return plugcfgDir + L"\\WhiteoutDex_Settings.ini";
}

// Where a relative entry is rooted. The Settings dialog's own MPQ directory
// field first, then the CASC install root - a Classic install keeps its
// archives in the install root itself, so a user who filled in only the one
// path still gets a working list. Mirrors the same fallback in
// mdlx_import_options.cpp.
inline std::wstring ArchiveDirectory(const std::wstring& plugcfgDir) {
    if (plugcfgDir.empty())
        return {};
    std::wstring dir = detail::IniString(plugcfgDir + L"\\WhiteoutDexMPQ.ini", L"MPQ", L"Directory");
    if (dir.empty())
        dir = detail::IniString(SettingsIniPath(plugcfgDir), L"CASC", L"W3Path");
    return dir;
}

// The list exactly as the user typed it, in priority order. Empty means the
// key is absent - "never customised", NOT "no archives".
inline std::vector<std::wstring> LoadList(const std::wstring& plugcfgDir) {
    const std::wstring raw = detail::IniString(SettingsIniPath(plugcfgDir), L"MPQ", L"List");
    std::vector<std::wstring> out;
    for (size_t start = 0; start <= raw.size();) {
        const size_t bar = raw.find(L'|', start);
        const size_t end = (bar == std::wstring::npos) ? raw.size() : bar;
        std::wstring entry = raw.substr(start, end - start);
        detail::TrimInPlace(entry);
        if (!entry.empty())
            out.push_back(std::move(entry));
        if (bar == std::wstring::npos)
            break;
        start = bar + 1;
    }
    return out;
}

// One entry as a path. An absolute entry is the answer on its own - that is
// the whole point of allowing them - and operator/ already does exactly that,
// so the branch is only here to say so out loud.
inline std::wstring Resolve(const std::wstring& entry, const std::wstring& baseDir) {
    if (entry.empty())
        return {};
    const std::filesystem::path p(entry);
    if (p.is_absolute() || baseDir.empty())
        return p.wstring();
    return (std::filesystem::path(baseDir) / p).wstring();
}

// The configured order as absolute paths to archives that are actually there,
// ready to hand to a storage opener. Empty when nothing is configured (see the
// note on LoadList) and also when every entry is missing, which reads the same
// way to a caller: keep your own default.
//
// Entries that do not resolve to a file are dropped rather than reported. The
// place to tell the user about a stale entry is the editor that shows the
// list, which marks it - a texture lookup failing three layers down is not.
inline std::vector<std::wstring> ResolvedArchives(const std::wstring& plugcfgDir) {
    const std::vector<std::wstring> entries = LoadList(plugcfgDir);
    if (entries.empty())
        return {};

    const std::wstring base = ArchiveDirectory(plugcfgDir);
    std::vector<std::wstring> out;
    out.reserve(entries.size());
    std::error_code ec;
    for (const std::wstring& e : entries) {
        std::wstring full = Resolve(e, base);
        if (full.empty())
            continue;
        if (!std::filesystem::is_regular_file(std::filesystem::path(full), ec))
            continue;
        out.push_back(std::move(full));
    }
    return out;
}

// UTF-8 form, for the engine-side APIs (FileContentProvider::SetMpqList,
// StorageBrowser::OpenArchives) which speak UTF-8 throughout.
inline std::vector<std::string> ResolvedArchivesUtf8(const std::wstring& plugcfgDir) {
    std::vector<std::string> out;
    for (const std::wstring& w : ResolvedArchives(plugcfgDir)) {
        const int len =
            ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 1)
            continue;
        std::string s(static_cast<size_t>(len - 1), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace wdx::mpq
