// MDLXImporter — Import options dialog
#pragma once

#include "mdlx_import_options.h"
#include <windows.h>

// Shows the import options dialog. Returns true if user clicked Import, false if Cancel.
// On return with true, opts is populated with the user's choices.
// |hInstance| is the DLL module handle (from DllMain).
// |hWndParent| is the Max main window.
// |isReforged| controls whether v1200-specific controls are visible.
// |hasCornEmitters| forces the Corn Emitters checkbox on regardless of version
// — Reforged exporters occasionally write CORN chunks while keeping the
// header version below 1200, and we still want the user to be able to toggle.
bool showImportDialog(HINSTANCE hInstance, HWND hWndParent,
                      MdlxImportOptions& opts, bool isReforged,
                      bool hasCornEmitters);
