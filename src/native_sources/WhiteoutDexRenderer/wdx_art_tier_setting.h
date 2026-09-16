#pragma once

// ============================================================================
// The preview's Warcraft III art tier, persisted beside the rest of the
// toolkit's settings.
//
//   <plugcfg>\WhiteoutDex_Settings.ini
//   [Renderer]
//   Wc3ArtTier=2
//
// Spelled the way the game's own `-hd` launch flag is: 0 Classic, 1 Reforged,
// 2 Definitive. An ABSENT key means "follow the render mode" (Classic for an
// SD model, Reforged for an HD one), which is what the preview did before
// 3.0.0 added a third tier, so a settings file that predates this reads the
// same art it always did.
//
// Not the WhiteoutFlakes.ini the View menu's other toggles go to: that file
// sits beside 3dsmax.exe and nothing in this plug-in ever reads it back. This
// one is in plugcfg, which is writable and is where the W3Path the tier
// resolves against already lives.
//
// Header-only because two TUs need the same key and the same parse rule:
// dllmain.cpp restores it on Start (Max thread), max_plugin_ui.cpp writes it
// when the menu changes (render thread).
// ============================================================================

#include "whiteout/flakes/enums.h" // Wc3ArtTier

#include <windows.h>

#include <cwchar>
#include <optional>
#include <string>

namespace wdx::renderer {

inline constexpr const wchar_t* kArtTierSection = L"Renderer";
inline constexpr const wchar_t* kArtTierKey = L"Wc3ArtTier";

inline std::optional<whiteout::flakes::Wc3ArtTier> LoadArtTier(const std::wstring& iniPath) {
    if (iniPath.empty() || GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return std::nullopt;

    // The exact Win32 signature, not a plain call: the 2026 Max SDK's
    // Util/IniUtil.h adds same-named overloads that make one ambiguous. Same
    // trick as ReadUserInstallPath and wdx_mpq_settings.h.
    static auto Win32_GetPrivateProfileStringW =
        static_cast<DWORD(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR)>(
            &::GetPrivateProfileStringW);
    wchar_t buf[16] = {};
    Win32_GetPrivateProfileStringW(kArtTierSection, kArtTierKey, L"", buf, 16, iniPath.c_str());
    if (!buf[0])
        return std::nullopt;

    // Out of range is "follow the render mode" too, the same rule the
    // standalone viewer applies to its own copy of this key.
    const int v = static_cast<int>(std::wcstol(buf, nullptr, 10));
    if (v < 0 || v > static_cast<int>(whiteout::flakes::Wc3ArtTier::Definitive))
        return std::nullopt;
    return static_cast<whiteout::flakes::Wc3ArtTier>(v);
}

// Unset removes the key rather than writing a number for it: absent is the
// honest spelling of "follow the render mode", and a number would turn the
// default into a choice nobody made.
inline void SaveArtTier(const std::wstring& iniPath,
                        std::optional<whiteout::flakes::Wc3ArtTier> tier) {
    if (iniPath.empty())
        return;
    static auto Win32_WritePrivateProfileStringW =
        static_cast<BOOL(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR)>(
            &::WritePrivateProfileStringW);
    if (tier) {
        const std::wstring value = std::to_wstring(static_cast<int>(*tier));
        Win32_WritePrivateProfileStringW(kArtTierSection, kArtTierKey, value.c_str(),
                                         iniPath.c_str());
    } else {
        Win32_WritePrivateProfileStringW(kArtTierSection, kArtTierKey, nullptr, iniPath.c_str());
    }
}

} // namespace wdx::renderer
