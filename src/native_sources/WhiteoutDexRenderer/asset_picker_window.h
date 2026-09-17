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
#include <vector>

#include <windows.h>

namespace whiteout::flakes {

/// One install to offer by name in the picker's File menu. Mirrors
/// tools::NamedRoot, and is repeated here so this header - the one the
/// MaxScript primitives include - does not have to pull in the whole
/// StorageExplorer (and with it RenderService, the thumbnail pool and gfx).
struct AssetPickerRoot {
    std::string label; ///< "Warcraft III Reforged"
    std::string root;  ///< the install directory
};

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
/// Every string it shows, the caption included, is resolved inside: the UI
/// catalog is (re)loaded here, once the preview is parked, so a caller that
/// translated something beforehand could be holding a bare key or a pointer
/// the reload freed.
///
/// @param types       which file types to list (io::BrowseType::Models,
///                    Effects, Textures, …). Also names the window.
/// @param cascRoot    Warcraft III install root — either generation, CASC or a
///                    directory of MPQs
/// @param initialRel  a relative path to reveal on open; "" starts at the root
/// @param initialFilter  text to seed the panel's search box with, in
///                    io::MatchesFilter syntax. What a caller that knows more
///                    than the type mask does with it: a material's normal-map
///                    slot opens on `_normal` rather than on every texture the
///                    game ships. "" leaves the box empty.
/// @param roots       installs to list by name at the top of the File menu, so
///                    a machine with both generations of Warcraft III can be
///                    switched between in one click. `cascRoot` is still what
///                    opens; these are the alternatives. Empty leaves the menu
///                    with just the folder picker.
/// @param archives    the host's MPQ load order - absolute paths, highest
///                    priority first. What makes an archive outside the game
///                    folder appear in the picker at all, and what keeps the
///                    picker showing the same set the extractor behind it
///                    reads. io::OpenWithArchives has the rule for how this
///                    combines with `cascRoot`. Empty means the user never
///                    configured one.
AssetPickResult RunAssetPicker(io::BrowseType types, const std::string& cascRoot,
                               const std::string& initialRel,
                               const std::string& initialFilter = {},
                               const std::vector<AssetPickerRoot>& roots = {},
                               const std::vector<std::string>& archives = {});

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
/// locate the `shaders/` pack the installer drops at the bundle root.
/// Defined in dllmain.cpp.
HINSTANCE WdxPluginInstance();

} // namespace whiteout::flakes
