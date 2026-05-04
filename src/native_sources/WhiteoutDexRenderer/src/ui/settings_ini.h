// ============================================================================
// settings_ini.h — Persist Background colour + Exposure across runs.
//
// Stores values in `WhiteoutFlakes.ini` next to the standalone executable.
// Format is the standard Win32 .ini layout consumed by the
// GetPrivateProfileString family:
//
//   [Display]
//   BackgroundColor=0x00453A35   ; sRGB packed 0x00BBGGRR (RenderService raw)
//   Exposure=1.000               ; tonemap exposure multiplier
//   SoundVolume=1.000            ; SND EventObject master gain (0..1)
//
// LoadSettingsIni is best-effort — missing file or missing keys leave the
// service's compile-time defaults untouched. SaveSettingsIni overwrites
// just the two keys, preserving any extra sections a future revision adds.
// ============================================================================
#pragma once

namespace WhiteoutDex {

class RenderService;

// Read the INI from <exeDir>/WhiteoutFlakes.ini and push the saved
// background colour + exposure into the live RenderService. Call this
// once at standalone startup, after the service is constructed and
// before the window opens so the toolbar / Settings popup pick up the
// restored values when they're created.
void LoadSettingsIni(RenderService& service);

// Write the current background colour + exposure to the INI. Call this
// from each per-control change handler — INI writes are cheap, no
// debouncing needed for a two-key file.
void SaveSettingsIni(const RenderService& service);

} // namespace WhiteoutDex
