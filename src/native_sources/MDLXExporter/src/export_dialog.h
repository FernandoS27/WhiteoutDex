// MDLXExporter — Export options dialog
#pragma once

#include "mdx_export_options.h"
#include <windows.h>

// Shows the export options dialog. Returns true if user clicked Export, false if Cancel.
// On return with true, opts is populated with the user's choices.
// |hInstance| is the DLL module handle (from DllMain).
// |hWndParent| is the Max main window.
bool showExportDialog(HINSTANCE hInstance, HWND hWndParent, MdxExportOptions& opts);
