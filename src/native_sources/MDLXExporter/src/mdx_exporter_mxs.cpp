// MDLXExporter — MaxScript interface
//
// Provides:
//   mdxExport()  — Show export options dialog, then save dialog, then export.
//                   Returns true on success, false on cancel/failure.
//
// This gives MaxScript callers a "dialog-first" workflow:
//   1. Export options dialog appears immediately
//   2. User configures settings, clicks Export
//   3. Save-file dialog appears
//   4. Export runs with selected path and options
//
// The traditional File > Export path (DoExport) remains unchanged.

#include <max.h>
#include <maxscript/maxscript.h>
#include <maxscript/util/listener.h>
#include <maxscript/foundation/numbers.h>
#include <maxscript/macros/define_instantiation_functions.h>

#include "mdx_exporter_plugin.h"
#include "mdx_export_options.h"
#include "export_dialog.h"

#include <commdlg.h>

extern HINSTANCE GetDllInstance();

// ============================================================================
// mdxExport()
// ============================================================================

def_visible_primitive(mdxExport, "mdxExport");

Value* mdxExport_cf(Value** arg_list, int count)
{
    check_arg_count(mdxExport, 0, count);

    Interface* gi = GetCOREInterface();
    if (!gi)
        return &false_value;

    // 1. Show the export options dialog (loads/saves INI internally)
    MdxExportOptions opts;
    if (!showExportDialog(GetDllInstance(), gi->GetMAXHWnd(), opts))
        return &false_value;

    // 2. Show save-file dialog
    OPENFILENAMEW ofn{};
    wchar_t szFile[MAX_PATH]{};

    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = gi->GetMAXHWnd();
    ofn.hInstance    = GetDllInstance();
    ofn.lpstrFile    = szFile;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrFilter  = L"Warcraft III Binary (*.mdx)\0*.mdx\0"
                       L"Warcraft III Text (*.mdl)\0*.mdl\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt  = L"mdx";
    ofn.lpstrTitle   = L"Save MDX/MDL Model";
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&ofn))
        return &false_value;

    // 3. Run the export with prompts suppressed — the dialog already saved
    //    settings to INI, and DoExport will reload them.
    MdxExporterPlugin exporter;
    int result = exporter.DoExport(szFile, nullptr, gi, TRUE, 0);

    return (result == IMPEXP_SUCCESS) ? &true_value : &false_value;
}
