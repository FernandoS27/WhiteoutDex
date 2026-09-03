// ============================================================================
// wdx_window_icon.h - puts the WhiteoutDex icon on a plug-in's own windows.
//
// Every plug-in that opens a window of its own embeds the shared icon under
// IDI_WHITEOUTDEX_ICON (see each .rc) and calls ApplyWindowIcon from
// WM_INITDIALOG.
//
// The dialog templates use DS_MODALFRAME, which carries WS_EX_DLGMODALFRAME,
// and Windows draws no title bar icon on a window that has that bit. Clearing
// it is what makes the icon show up; under DWM the frame itself renders the
// same either way, so nothing else about the dialog changes.
// ============================================================================
#pragma once

#include <windows.h>

namespace wdx {

// `inst` defaults to the module that owns the window, which for a DialogBox is
// the module the template came from - i.e. the plug-in holding the icon.
inline void ApplyWindowIcon(HWND hwnd, int iconId, HINSTANCE inst = nullptr) {
    if (!hwnd)
        return;
    if (!inst)
        inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
    if (!inst)
        return;

    HICON icon = LoadIconW(inst, MAKEINTRESOURCEW(iconId));
    if (!icon)
        return;

    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_DLGMODALFRAME) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_DLGMODALFRAME);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    // The HICON belongs to the module's resources, so it outlives the window
    // and must not be destroyed here.
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
}

} // namespace wdx
