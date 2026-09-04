// MDLXExporter — Export options dialog
#pragma once

#include "mdx_export_options.h"
#include <windows.h>
#include <string>

// Name of the currently open scene with the directory and the .max extension
// stripped ("D:\work\arquebus_21.max" -> "arquebus_21"). Empty when the scene
// has never been saved. This is the default MODL name for an export.
std::wstring currentSceneModelName();

// Shows the export options dialog. Returns true if user clicked Export, false if Cancel.
// On return with true, opts is populated with the user's choices.
// |hInstance| is the DLL module handle (from DllMain).
// |hWndParent| is the Max main window.
bool showExportDialog(HINSTANCE hInstance, HWND hWndParent, MdxExportOptions& opts);
