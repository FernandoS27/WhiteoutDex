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
#include "scene_monitor.h"
#include <string>
#include <set>
#include <setkeymode.h>

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

// ============================================================================
// wdxSceneScan() / wdxSceneFixAll()
// ============================================================================
//
// The export dialog's scene check and its "Fix all" button for scripts - a
// batch export (exportFile ... #noPrompt) has no dialog to press it in.
//   wdxSceneScan()   -> one line per problem, "<type>: <object>", "" if none
//   wdxSceneFixAll() -> number of problems fixed (the same scene_monitor::fixAll
//                       the button runs, on a fresh scan)

static const wchar_t* problemTypeName(scene_monitor::ProblemType t)
{
    using scene_monitor::ProblemType;
    switch (t) {
    case ProblemType::DuplicateName:       return L"DuplicateName";
    case ProblemType::EmptyMesh:           return L"EmptyMesh";
    case ProblemType::EditablePoly:        return L"EditablePoly";
    case ProblemType::EditMeshAboveSkin:   return L"EditMeshAboveSkin";
    case ProblemType::InvalidController:   return L"InvalidController";
    case ProblemType::UnsupportedMaterial: return L"UnsupportedMaterial";
    case ProblemType::MultiMaterialMesh:   return L"MultiMaterialMesh";
    case ProblemType::DuplicateMaterial:   return L"DuplicateMaterial";
    }
    return L"Unknown";
}

def_visible_primitive(wdxSceneScan, "wdxSceneScan");

Value* wdxSceneScan_cf(Value** arg_list, int count)
{
    check_arg_count(wdxSceneScan, 0, count);
    const scene_monitor::ScanResult result = scene_monitor::scanScene();
    std::wstring text;
    for (const auto& p : result.problems) {
        if (!text.empty()) text += L"\n";
        text += problemTypeName(p.type);
        text += L": ";
        text += p.displayName;
    }
    return new String(text.c_str());
}

// wdxSetKeyBuffers() - diagnostic: the tracks that hold a Set Key buffer
// (Animatable::SetKeyBufferPresent), one "<track path> (<class>)" per line;
// first line "present: true|false" from SetKeyModeInterface. A buffer makes
// the Set Keys button blink until it is committed or reverted.
static void collectSetKeyBuffers(Animatable* anim, const std::wstring& path, int depth,
                                 std::set<Animatable*>& seen, std::wstring& out)
{
    if (!anim || depth > 24 || !seen.insert(anim).second) return;
    if (anim->SetKeyBufferPresent()) {
        MSTR cls;
#if MAX_PRODUCT_YEAR_NUMBER >= 2023
        anim->GetClassName(cls, false);
#else
        anim->GetClassName(cls);
#endif
        out += L"\n" + path + L" (" + std::wstring(cls.data()) + L")";
    }
    for (int i = 0; i < anim->NumSubs(); ++i) {
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
        MSTR sub = anim->SubAnimName(i, false);
#else
        MSTR sub = anim->SubAnimName(i);
#endif
        collectSetKeyBuffers(anim->SubAnim(i), path + L"/" + std::wstring(sub.data()), depth + 1, seen, out);
    }
}

def_visible_primitive(wdxSetKeyBuffers, "wdxSetKeyBuffers");

Value* wdxSetKeyBuffers_cf(Value** arg_list, int count)
{
    check_arg_count(wdxSetKeyBuffers, 0, count);
    Interface* ip = GetCOREInterface();
    SetKeyModeInterface* sk = GetSetKeyModeInterface(ip);
    std::wstring text = L"present: ";
    text += (sk && sk->AllTracksSetKeyBufferPresent()) ? L"true" : L"false";
    std::set<Animatable*> seen;
    collectSetKeyBuffers(ip->GetRootNode(), L"root", 0, seen, text);
    return new String(text.c_str());
}

def_visible_primitive(wdxSceneFixAll, "wdxSceneFixAll");

Value* wdxSceneFixAll_cf(Value** arg_list, int count)
{
    check_arg_count(wdxSceneFixAll, 0, count);
    const scene_monitor::ScanResult result = scene_monitor::scanScene();
    return Integer::intern(result.empty() ? 0 : scene_monitor::fixAll(result));
}
