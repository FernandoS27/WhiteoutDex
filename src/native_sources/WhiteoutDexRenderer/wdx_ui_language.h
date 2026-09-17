// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 WhiteoutDex Contributors

#pragma once

/**
 * @file wdx_ui_language.h
 * @brief Points the WhiteoutFlakes i18n catalog at WhiteoutDex's own settings.
 *
 * The renderer window and the asset picker are the WhiteoutFlakes viewer's
 * chrome hosted inside 3ds Max, so their strings live in the submodule's
 * `resources/lang/<code>.ini` catalogs — eleven languages, already translated —
 * and not in WhiteoutDexLocalization.ms. What this header adds is the two ends
 * that differ inside Max:
 *
 *   - the catalogs are found relative to this .dlx, not to 3dsmax.exe, since
 *     `flakes::AssetDir()` resolves next to the running executable;
 *   - the active language comes from WhiteoutDex's own preference,
 *     `<plugcfg>\WhiteoutDex_Settings.ini` → [Localization] Language, the key
 *     the Settings dialog writes — so one language choice drives the whole
 *     toolkit, MaxScript UI and renderer alike.
 *
 * WhiteoutDex offers ten languages and Flakes eleven; the odd one out
 * (Traditional Chinese) simply never comes up here, and an unrecognised code
 * falls back to English the same way `WdxL.t` does.
 */

#include "localization.h"
#include "wdx_install_layout.h"

#include "whiteout/flakes/util/path_utf8.h"

#include <filesystem>
#include <string>

#include <windows.h>

namespace wdx::ui {

namespace detail {
/// Whether InitLanguage has run this session. Max UI thread only, which is
/// where every caller is.
inline bool g_languageLoaded = false;
} // namespace detail

/// Read `[Localization] Language` out of WhiteoutDex_Settings.ini in
/// @p plugcfgDir. Empty when the file or the key is absent, which means
/// "never chosen" and resolves to English.
inline std::string ReadConfiguredLanguage(const std::wstring& plugcfgDir) {
    if (plugcfgDir.empty())
        return {};
    const std::wstring ini = plugcfgDir + L"\\WhiteoutDex_Settings.ini";
    if (::GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES)
        return {};

    // Disambiguate the Win32 API from the MaxSDK::Util overload the 2026 SDK
    // added — same trick dllmain.cpp uses for the CASC path read.
    static auto Win32_GetPrivateProfileStringW =
        static_cast<DWORD(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR)>(
            &::GetPrivateProfileStringW);

    wchar_t buf[32] = {};
    Win32_GetPrivateProfileStringW(L"Localization", L"Language", L"", buf,
                                   static_cast<DWORD>(std::size(buf)), ini.c_str());
    // Language codes are ASCII ("en", "zh", "pt-br"), so the narrowing is safe.
    std::string out;
    for (const wchar_t* p = buf; *p; ++p)
        out.push_back(static_cast<char>(*p));
    return out;
}

/// Load the Flakes UI catalogs and activate WhiteoutDex's configured language.
///
/// `lang/` is looked for beside the .dlx first so a hand-copied plug-in folder
/// still finds it, then at the installed location two levels up. Missing
/// catalogs leave every tr() returning its key verbatim.
///
/// This REloads: every pointer tr() handed out before it is freed. Call it
/// only while the preview's render thread is not building a frame - before it
/// starts, or with it parked (WdxPreviewPause). Anywhere else, use
/// EnsureLanguageLoaded.
inline void InitLanguage(HINSTANCE hInstance, const std::wstring& plugcfgDir) {
    namespace i18n = whiteout::flakes::i18n;

    std::filesystem::path langDir;
    wchar_t buf[MAX_PATH] = {};
    if (::GetModuleFileNameW(hInstance, buf, MAX_PATH) > 0) {
        const std::filesystem::path dlxDir = std::filesystem::path(buf).parent_path();
        std::error_code ec;
        if (std::filesystem::is_directory(dlxDir / "lang", ec))
            langDir = dlxDir / "lang";
    }
    if (langDir.empty()) {
        const std::filesystem::path root = wdx::InstallRootFromModule(hInstance);
        if (!root.empty())
            langDir = root / "lang";
    }

    const auto lang = i18n::languageFromCode(ReadConfiguredLanguage(plugcfgDir));
    i18n::Localizer::instance().load(
        langDir.empty() ? std::string{} : whiteout::flakes::io::PathToUtf8(langDir), lang);
    detail::g_languageLoaded = true;
}

/// InitLanguage, unless something already ran it this session.
///
/// For the strings a MaxScript primitive returns, which can be asked for while
/// the preview is drawing and so must never reload under it. That is safe by
/// ordering: the preview loads the catalog before its render thread exists, so
/// if it is up this does nothing, and if it is not nobody else reads the
/// catalog. The cost is that a language changed in Settings reaches these
/// strings only once something reloads - the preview's next start, or the
/// picker.
inline void EnsureLanguageLoaded(HINSTANCE hInstance, const std::wstring& plugcfgDir) {
    if (!detail::g_languageLoaded)
        InitLanguage(hInstance, plugcfgDir);
}

} // namespace wdx::ui
