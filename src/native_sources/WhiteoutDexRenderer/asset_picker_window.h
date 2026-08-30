#pragma once

// ============================================================================
// AssetPickerWindow — a modal "pick a model / effect out of the game archives"
// dialog, hosting WhiteoutFlakes' StorageExplorer panel.
//
// StorageExplorer is the real browser: a CASC tree plus a thumbnail pool that
// renders every visible cell as a live model in its own scene, shown either as
// a grid of icons or as an outline beside one large preview. It owns no window,
// no gfx device and no ImGui context — a host supplies all three plus a
// RenderService and drives NewFrame / BuildWindow / RenderThumbnails each
// frame. That is all this file is.
//
// It runs on the calling (Max UI) thread with its own message pump and the
// owner window disabled, so from MaxScript's side the call simply blocks and
// returns a path — the same shape getOpenFileName has.
// ============================================================================

#include "io/storage_browser.h" // io::BrowseType

#include <string>

#include <windows.h>

namespace whiteout::flakes {

struct AssetPickResult {
    bool accepted = false;
    /// Mod-chain stripped, '\'-separated — the form MDX stores and the content
    /// provider reads back ("units\nightelf\druid\druid.mdx").
    std::string relPath;
    /// The CASC-native archive path, for a caller that wants to hand it
    /// straight to a loader.
    std::string archivePath;
    /// Set when the picker could not run at all; empty on a normal
    /// pick-or-cancel.
    std::string error;
};

/// Show the picker and block until the user chooses or closes it.
///
/// @param title       window caption
/// @param types       which file types to list (io::BrowseType::Models, Effects, …)
/// @param cascRoot    Warcraft III install root to open
/// @param initialRel  a relative path to reveal on open; "" starts at the root
AssetPickResult RunAssetPicker(const std::wstring& title, io::BrowseType types,
                               const std::string& cascRoot, const std::string& initialRel);

/// Parks the preview render window for as long as it is alive, and disables it
/// so the user cannot drive a window that has stopped drawing.
///
/// Each window owns its own ImGui context and selects it before every ImGui
/// call, which is what makes two of them legal at all. What is still shared is
/// which context is CURRENT: that is one process-global pointer, and the
/// preview drives its frames from its own thread. Rather than making that
/// pointer thread-local for the whole engine, the picker simply makes sure the
/// preview is not using ImGui while it is: SuspendForModal blocks until the
/// render thread is parked outside its ImGui section.
///
/// Constructing one when no preview is running is free and does nothing.
/// Defined in dllmain.cpp, which is where the preview window lives.
class WdxPreviewPause {
public:
    WdxPreviewPause();
    ~WdxPreviewPause();
    WdxPreviewPause(const WdxPreviewPause&) = delete;
    WdxPreviewPause& operator=(const WdxPreviewPause&) = delete;

private:
    bool paused_ = false;
    void* hwnd_ = nullptr; // HWND re-enabled on destruction; null when none
};

/// This .dlx's HINSTANCE — the picker needs it for its window class and to
/// locate the `shaders/` directory the installer drops beside the plug-in.
/// Defined in dllmain.cpp.
HINSTANCE WdxPluginInstance();

} // namespace whiteout::flakes
