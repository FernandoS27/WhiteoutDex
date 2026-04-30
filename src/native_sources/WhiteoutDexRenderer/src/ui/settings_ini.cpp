// ============================================================================
// settings_ini.cpp — see settings_ini.h
// ============================================================================
#include "settings_ini.h"

#include "../renderer/render_service.h"

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
}

} // namespace WhiteoutDex
