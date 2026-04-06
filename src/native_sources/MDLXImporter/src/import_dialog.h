// MDLXImporter — Import options dialog
#pragma once

#include "mdlx_import_options.h"
#include <windows.h>

// Shows the import options dialog. Returns true if user clicked Import, false if Cancel.
// On return with true, opts is populated with the user's choices.
// |hInstance| is the DLL module handle (from DllMain).
// |hWndParent| is the Max main window.
// |isReforged| controls whether v1200-specific controls (Corn Emitters, FaceFX) are visible.
bool showImportDialog(HINSTANCE hInstance, HWND hWndParent,
                      MdlxImportOptions& opts, bool isReforged);
