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

    // 1. Show the export options dialog (loads/saves INI internally). It stays
    //    open after Export and shows the export's progress in step 3.
    MdxExportOptions opts;
    ExportDialog dialog(GetDllInstance(), gi->GetMAXHWnd());
    if (!dialog.run(opts))
        return &false_value;

    // 2. Show save-file dialog, owned by the still open options dialog so
    //    that one cannot be clicked meanwhile.
    OPENFILENAMEW ofn{};
    wchar_t szFile[MAX_PATH]{};

    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = dialog.window() ? dialog.window() : gi->GetMAXHWnd();
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
    exporter.setProgressDialog(&dialog);
    int result = exporter.DoExport(szFile, nullptr, gi, TRUE, 0);

    return (result == IMPEXP_SUCCESS) ? &true_value : &false_value;
}
