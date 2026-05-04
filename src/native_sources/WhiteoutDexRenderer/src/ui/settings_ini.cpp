// ============================================================================
// settings_ini.cpp — see settings_ini.h
// ============================================================================
#include "settings_ini.h"

#include "../renderer/render_service.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <string>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace WhiteoutDex {

namespace {

// Build the absolute path to <exeDir>/WhiteoutFlakes.ini using the
// Win32 module path so the INI follows the binary regardless of where
// the working directory points (drag-drop launches set CWD to the
// dropped file's folder, not the exe's).
std::wstring SettingsIniPath() {
    wchar_t exePath[MAX_PATH] = {};
    DWORD n = ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"WhiteoutFlakes.ini";
    std::filesystem::path p(exePath);
    p.replace_filename(L"WhiteoutFlakes.ini");
    return p.wstring();
}

constexpr const wchar_t* kSection = L"Display";

} // namespace

void LoadSettingsIni(RenderService& service) {
    const std::wstring iniPath = SettingsIniPath();

    // BackgroundColor — stored as a hex literal so the value matches what
    // the user sees in source / debugger; accept "0x...." or bare hex.
    {
        wchar_t buf[32] = {};
        ::GetPrivateProfileStringW(kSection, L"BackgroundColor", L"",
                                   buf, 32, iniPath.c_str());
        if (buf[0]) {
            wchar_t* endptr = nullptr;
            const unsigned long val = std::wcstoul(buf, &endptr, 16);
            if (endptr != buf) {
                const uint32_t v = static_cast<uint32_t>(val);
                // RenderService stores COLORREF-packed (0x00BBGGRR).
                // SetBackgroundColor takes (r, g, b) as bytes.
                service.SetBackgroundColor(
                    static_cast<uint8_t>(v        & 0xFF),  // R
                    static_cast<uint8_t>((v >> 8) & 0xFF),  // G
                    static_cast<uint8_t>((v >> 16) & 0xFF));// B
            }
        }
    }

    // Exposure — float in [0, 3] per the Settings slider's range.
    {
        wchar_t buf[32] = {};
        ::GetPrivateProfileStringW(kSection, L"Exposure", L"",
                                   buf, 32, iniPath.c_str());
        if (buf[0]) {
            wchar_t* endptr = nullptr;
            const double val = std::wcstod(buf, &endptr);
            if (endptr != buf) {
                // Clamp into the slider's published range so a hand-edited
                // out-of-range value doesn't push the trackbar to a
                // position it can't represent.
                float clamped = static_cast<float>(val);
                if (clamped < 0.0f) clamped = 0.0f;
                if (clamped > 3.0f) clamped = 3.0f;
                service.SetTonemapExposure(clamped);
            }
        }
    }

    // Sound volume — float in [0, 1]. Forwarded to the active
    // ISoundEmitter; RenderService caches it so a backend installed
    // *after* this load (the standalone's WindowsSoundEmitter is
    // installed post-construction) still picks the value up.
    {
        wchar_t buf[32] = {};
        ::GetPrivateProfileStringW(kSection, L"SoundVolume", L"",
                                   buf, 32, iniPath.c_str());
        if (buf[0]) {
            wchar_t* endptr = nullptr;
            const double val = std::wcstod(buf, &endptr);
            if (endptr != buf) {
                float clamped = static_cast<float>(val);
                if (clamped < 0.0f) clamped = 0.0f;
                if (clamped > 1.0f) clamped = 1.0f;
                service.SetSoundVolume(clamped);
            }
        }
    }

    // LoopNonLooping — boolean stored as "0"/"1". Toggles the
    // per-actor ignoreNonLooping override globally.
    {
        const int v = ::GetPrivateProfileIntW(kSection, L"LoopNonLooping",
                                              -1, iniPath.c_str());
        if (v == 0 || v == 1) service.SetIgnoreNonLooping(v != 0);
    }

    // View menu toggles — Grid / Particles / PopcornFX / Ribbons /
    // Event Objects. Stored as "0"/"1"; sentinel -1 leaves the
    // service's compile-time default in place. The render window's
    // menu is built from the same DisplayFlags before this load runs,
    // so callers must invoke RenderWindow::SyncViewMenuFromService()
    // afterwards to repaint the checkmarks.
    {
        DisplayFlags df = service.GetDisplayFlags();
        bool dirty = false;
        auto loadFlag = [&](const wchar_t* key, bool& field) {
            const int v = ::GetPrivateProfileIntW(kSection, key, -1,
                                                  iniPath.c_str());
            if (v == 0 || v == 1) {
                field = (v != 0);
                dirty = true;
            }
        };
        loadFlag(L"ShowGrid",      df.showGrid);
        loadFlag(L"ShowParticles", df.showParticles);
        loadFlag(L"ShowRibbons",   df.showRibbons);
        loadFlag(L"ShowEvents",    df.showEvents);
        if (dirty) service.SetDisplayFlags(df);
    }

    // Tileset — integer matching io::Tileset (0..Count-1). Sentinel -1
    // leaves the service's default (LordaeronSummer). Drives the
    // replaceable-cliff path lookup; UI menu is re-synced afterwards.
    {
        const int v = ::GetPrivateProfileIntW(kSection, L"Tileset", -1,
                                              iniPath.c_str());
        const int n = static_cast<int>(io::Tileset::Count);
        if (v >= 0 && v < n) service.SetTileset(static_cast<io::Tileset>(v));
    }

    // IblMode — integer matching the IblMode enum (0=Portrait,
    // 1=DayNight, 2=Dungeon, 3=Sunset). Sentinel -1 leaves the
    // service's default in place (Portrait), so a missing/unparseable
    // key won't yank a user into a different probe unexpectedly.
    //
    // Skip the call when the saved value already matches the active
    // mode: SetIblMode tears down + reloads the IBL probe pair, and
    // doing that on the main thread immediately after the render
    // thread comes up (Open returns the moment the first frame is
    // queued) races with in-flight HD draws that hold the old probe
    // SRV — manifesting as a black/garbled probe until the user
    // re-picks one through the UI. The default `Portrait` already
    // matches what InitBlsShaders just loaded, so a saved
    // `IblMode=0` would otherwise force a useless reload every
    // launch.
    {
        const int v = ::GetPrivateProfileIntW(kSection, L"IblMode",
                                              -1, iniPath.c_str());
        if (v >= 0 && v <= static_cast<int>(IblMode::Sunset)
            && static_cast<IblMode>(v) != service.GetIblMode()) {
            service.SetIblMode(static_cast<IblMode>(v));
        }
    }

    // ShadowCascades — 0..3. Deliberately NOT restored on startup:
    // toggling the cascade service ON before the focus model has
    // loaded triggers a slow GPU TDR ("hangs after a while") that
    // toggling at runtime through the Settings combo doesn't, and
    // the root cause is still under investigation. Saving the
    // value via SaveSettingsIni still works so the user's preference
    // doesn't get lost — it's just not auto-applied next launch.
    // Re-enable by deleting this guard once the startup race / state
    // accumulation is properly diagnosed.
    {
        const int v = ::GetPrivateProfileIntW(kSection, L"ShadowCascades",
                                              -1, iniPath.c_str());
        (void)v;
    }

    // DNC TOD knobs. The DncService is constructed lazily inside
    // InitBlsShaders, *after* this Load call runs in the standalone
    // host. We can't write to it yet, so we stash the parsed values
    // and apply them in a deferred path — but that's overkill for
    // these three settings. Instead, since the standalone calls
    // LoadSettingsIni *before* the renderer has a content provider,
    // and ApplyDncSettingsToService is invoked once the service is
    // up, we just read into static-life cache here. For now: skip if
    // service is unwired, leaving the saved values to be re-applied
    // when the user re-saves them through the UI.
    if (auto* dnc = service.GetDncService()) {
        wchar_t buf[32] = {};
        ::GetPrivateProfileStringW(kSection, L"TimeOfDay", L"",
                                   buf, 32, iniPath.c_str());
        if (buf[0]) {
            wchar_t* endptr = nullptr;
            const double v = std::wcstod(buf, &endptr);
            if (endptr != buf) dnc->SetTimeOfDay(static_cast<float>(v));
        }
        const int animate = ::GetPrivateProfileIntW(kSection, L"AnimateTod",
                                                    -1, iniPath.c_str());
        if (animate == 0 || animate == 1) {
            dnc->SetTodScale(animate ? 1.0f : 0.0f);
        }
        // DNC MDL override path. Use a wide buffer because Environment/
        // DNC paths can be long (~80 chars). Apply only when the saved
        // value is non-empty AND differs from the default — applying
        // the same path triggers a reload that costs MDL parse +
        // hierarchy rebuild for nothing.
        wchar_t pathBuf[512] = {};
        ::GetPrivateProfileStringW(kSection, L"DncModel", L"",
                                   pathBuf, 512, iniPath.c_str());
        if (pathBuf[0]) {
            const int u8len = ::WideCharToMultiByte(CP_UTF8, 0, pathBuf, -1,
                                                    nullptr, 0, nullptr, nullptr);
            if (u8len > 1) {
                std::string utf8(u8len - 1, '\0');
                ::WideCharToMultiByte(CP_UTF8, 0, pathBuf, -1,
                                      utf8.data(), u8len, nullptr, nullptr);
                if (utf8 != dnc->UnitMdlPath()) dnc->SetUnitMdl(utf8);
            }
        }
    }
}

void SaveSettingsIni(const RenderService& service) {
    const std::wstring iniPath = SettingsIniPath();

    {
        wchar_t buf[32] = {};
        ::swprintf_s(buf, L"0x%08X",
                     static_cast<unsigned>(service.GetBackgroundColorRaw()));
        ::WritePrivateProfileStringW(kSection, L"BackgroundColor",
                                     buf, iniPath.c_str());
    }
    {
        wchar_t buf[32] = {};
        ::swprintf_s(buf, L"%.3f",
                     static_cast<double>(service.GetTonemapExposure()));
        ::WritePrivateProfileStringW(kSection, L"Exposure",
                                     buf, iniPath.c_str());
    }
    {
        wchar_t buf[32] = {};
        ::swprintf_s(buf, L"%.3f",
                     static_cast<double>(service.GetSoundVolume()));
        ::WritePrivateProfileStringW(kSection, L"SoundVolume",
                                     buf, iniPath.c_str());
    }
    {
        ::WritePrivateProfileStringW(kSection, L"LoopNonLooping",
                                     service.GetIgnoreNonLooping() ? L"1" : L"0",
                                     iniPath.c_str());
    }
    {
        const DisplayFlags df = service.GetDisplayFlags();
        auto saveFlag = [&](const wchar_t* key, bool v) {
            ::WritePrivateProfileStringW(kSection, key, v ? L"1" : L"0",
                                         iniPath.c_str());
        };
        saveFlag(L"ShowGrid",      df.showGrid);
        saveFlag(L"ShowParticles", df.showParticles);
        saveFlag(L"ShowRibbons",   df.showRibbons);
        saveFlag(L"ShowEvents",    df.showEvents);
    }
    {
        wchar_t buf[8] = {};
        ::swprintf_s(buf, L"%u",
                     static_cast<unsigned>(service.GetTileset()));
        ::WritePrivateProfileStringW(kSection, L"Tileset", buf, iniPath.c_str());
    }
    {
        wchar_t buf[8] = {};
        ::swprintf_s(buf, L"%u",
                     static_cast<unsigned>(service.GetIblMode()));
        ::WritePrivateProfileStringW(kSection, L"IblMode", buf, iniPath.c_str());
    }
    if (const auto* shadow = service.GetShadowService()) {
        const int cascades = shadow->IsEnabled()
                                 ? std::clamp(shadow->Params().cascadeCount, 1, 3)
                                 : 0;
        wchar_t buf[8] = {};
        ::swprintf_s(buf, L"%d", cascades);
        ::WritePrivateProfileStringW(kSection, L"ShadowCascades", buf, iniPath.c_str());
    }
    if (const auto* dnc = service.GetDncService()) {
        wchar_t buf[32] = {};
        ::swprintf_s(buf, L"%.3f", static_cast<double>(dnc->GetTimeOfDay()));
        ::WritePrivateProfileStringW(kSection, L"TimeOfDay", buf, iniPath.c_str());
        ::WritePrivateProfileStringW(kSection, L"AnimateTod",
                                     dnc->GetTodScale() > 0.0f ? L"1" : L"0",
                                     iniPath.c_str());
        // DNC MDL override path — UTF-8 → UTF-16 so Win32 ini APIs
        // round-trip non-ASCII chars cleanly.
        const std::string& path = dnc->UnitMdlPath();
        const int wlen = ::MultiByteToWideChar(CP_UTF8, 0,
                                               path.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wpath(wlen, L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1,
                                  wpath.data(), wlen);
            ::WritePrivateProfileStringW(kSection, L"DncModel",
                                         wpath.c_str(), iniPath.c_str());
        }
    }
}

} // namespace WhiteoutDex
