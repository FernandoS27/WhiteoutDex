// MDLXExporter — Export options dialog implementation
#include "export_dialog.h"
#include "resource.h"

#include <max.h>
#include <MaxDirectories.h>
#include <commctrl.h>
#include <string>
#include <cstdio>

// ============================================================================
// Dialog state passed via LPARAM → GWLP_USERDATA
// ============================================================================
struct DialogState {
    MdxExportOptions* opts;
    bool confirmed;
};

// ============================================================================
// Helpers
// ============================================================================

namespace {

void setCheck(HWND hDlg, int id, bool val) {
    CheckDlgButton(hDlg, id, val ? BST_CHECKED : BST_UNCHECKED);
}

bool getCheck(HWND hDlg, int id) {
    return IsDlgButtonChecked(hDlg, id) == BST_CHECKED;
}

void enableCtrl(HWND hDlg, int id, bool on) {
    EnableWindow(GetDlgItem(hDlg, id), on ? TRUE : FALSE);
}

// ── INI persistence ────────────────────────────────────────

std::wstring getINIPath() {
    auto* gi = GetCOREInterface();
    MSTR dir;
    if (gi)
        dir = gi->GetDir(APP_PLUGCFG_DIR);
    else {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring s(buf);
        auto pos = s.find_last_of(L'\\');
        dir = MSTR(s.substr(0, pos).c_str());
    }
    return std::wstring(dir.data()) + L"\\MDLXExporter.ini";
}

// Disambiguate Win32 API from MaxSDK::Util wrappers
static auto Win32_WritePrivateProfileStringW =
    static_cast<BOOL(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR)>(
        &::WritePrivateProfileStringW);

static auto Win32_GetPrivateProfileStringW =
    static_cast<DWORD(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR)>(
        &::GetPrivateProfileStringW);

std::wstring iniGetString(const wchar_t* path, const wchar_t* section,
                          const wchar_t* key, const wchar_t* def = L"")
{
    wchar_t buf[512];
    Win32_GetPrivateProfileStringW(section, key, def, buf, 512, path);
    return buf;
}

bool iniBool(const std::wstring& val) {
    return val == L"true" || val == L"True" || val == L"1";
}

// Save dialog settings to INI (same keys as macroscript for interop)
void saveDialogSettingsToINI(HWND hDlg) {
    std::wstring iniPath = getINIPath();
    const wchar_t* sec = L"Settings";

    auto writeB = [&](const wchar_t* key, bool val) {
        Win32_WritePrivateProfileStringW(sec, key, val ? L"true" : L"false", iniPath.c_str());
    };
    auto writeI = [&](const wchar_t* key, int val) {
        wchar_t buf[16];
        _itow_s(val, buf, 10);
        Win32_WritePrivateProfileStringW(sec, key, buf, iniPath.c_str());
    };
    auto writeS = [&](const wchar_t* key, const wchar_t* val) {
        Win32_WritePrivateProfileStringW(sec, key, val, iniPath.c_str());
    };

    // Model Name
    wchar_t nameText[256];
    GetDlgItemTextW(hDlg, IDC_EDT_MODEL_NAME, nameText, 256);
    writeS(L"ModelName", nameText);

    // Format Version (macroscript: 1=Classic, 2=Reforged)
    writeI(L"ExportVersion", getCheck(hDlg, IDC_RDO_REFORGED) ? 2 : 1);

    // Options
    writeB(L"MergeSimilarMeshes", getCheck(hDlg, IDC_CHK_MERGE_SIMILAR));
    writeB(L"FixNormals", getCheck(hDlg, IDC_CHK_FIX_NORMALS));
    writeB(L"ExportSmoothgroups", getCheck(hDlg, IDC_CHK_EXPORT_SMOOTHGROUPS));
    writeB(L"FixSharedNormals", getCheck(hDlg, IDC_CHK_FIX_SHARED_NORMALS));
    writeB(L"KeepUnusedBonesHelpers", getCheck(hDlg, IDC_CHK_KEEP_UNUSED_BH));

    // Threshold (float as string)
    wchar_t threshText[32];
    GetDlgItemTextW(hDlg, IDC_EDT_THRESHOLD, threshText, 32);
    writeS(L"Threshold", threshText);

    // Extents
    writeI(L"ExtentsType", getCheck(hDlg, IDC_RDO_EXTENTS_GLOBAL) ? 2 : 1);

    wchar_t precText[16];
    GetDlgItemTextW(hDlg, IDC_EDT_EXTENTS_PREC, precText, 16);
    writeS(L"ExtentsPrecision", precText);
}

// Load dialog settings from INI
void loadDialogSettingsFromINI(HWND hDlg) {
    std::wstring iniPath = getINIPath();

    if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;

    auto gs = [&](const wchar_t* key) {
        return iniGetString(iniPath.c_str(), L"Settings", key);
    };

    // Model Name
    auto modelName = gs(L"ModelName");
    if (!modelName.empty())
        SetDlgItemTextW(hDlg, IDC_EDT_MODEL_NAME, modelName.c_str());

    // Format Version
    auto ver = gs(L"ExportVersion");
    if (!ver.empty()) {
        int v = _wtoi(ver.c_str());
        setCheck(hDlg, IDC_RDO_CLASSIC, v != 2);
        setCheck(hDlg, IDC_RDO_REFORGED, v == 2);
    }

    // Options
    auto merge = gs(L"MergeSimilarMeshes");
    if (!merge.empty()) setCheck(hDlg, IDC_CHK_MERGE_SIMILAR, iniBool(merge));

    auto fixN = gs(L"FixNormals");
    if (!fixN.empty()) setCheck(hDlg, IDC_CHK_FIX_NORMALS, iniBool(fixN));

    auto fixSN = gs(L"FixSharedNormals");
    if (!fixSN.empty()) setCheck(hDlg, IDC_CHK_FIX_SHARED_NORMALS, iniBool(fixSN));

    auto smooth = gs(L"ExportSmoothgroups");
    if (!smooth.empty()) setCheck(hDlg, IDC_CHK_EXPORT_SMOOTHGROUPS, iniBool(smooth));

    auto keepBH = gs(L"KeepUnusedBonesHelpers");
    if (!keepBH.empty()) setCheck(hDlg, IDC_CHK_KEEP_UNUSED_BH, iniBool(keepBH));

    // Threshold
    auto thresh = gs(L"Threshold");
    if (!thresh.empty())
        SetDlgItemTextW(hDlg, IDC_EDT_THRESHOLD, thresh.c_str());

    // Extents
    auto extType = gs(L"ExtentsType");
    if (!extType.empty()) {
        int t = _wtoi(extType.c_str());
        setCheck(hDlg, IDC_RDO_EXTENTS_ANIM, t != 2);
        setCheck(hDlg, IDC_RDO_EXTENTS_GLOBAL, t == 2);
    }

    auto extPrec = gs(L"ExtentsPrecision");
    if (!extPrec.empty())
        SetDlgItemTextW(hDlg, IDC_EDT_EXTENTS_PREC, extPrec.c_str());
}

// ── Populate options struct from dialog ────────────────────

void dialogToOptions(HWND hDlg, MdxExportOptions& opts) {
    // Model Name
    wchar_t nameText[256];
    GetDlgItemTextW(hDlg, IDC_EDT_MODEL_NAME, nameText, 256);
    int len = WideCharToMultiByte(CP_UTF8, 0, nameText, -1, nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        opts.modelName.resize(static_cast<size_t>(len - 1));
        WideCharToMultiByte(CP_UTF8, 0, nameText, -1, opts.modelName.data(), len, nullptr, nullptr);
    }

    // Format Version
    opts.version = getCheck(hDlg, IDC_RDO_REFORGED) ? 1200 : 800;

    // Options
    opts.mergeGeosets = getCheck(hDlg, IDC_CHK_MERGE_SIMILAR);
    opts.fixNormals = getCheck(hDlg, IDC_CHK_FIX_NORMALS);
    opts.fixSharedNormals = getCheck(hDlg, IDC_CHK_FIX_SHARED_NORMALS);
    opts.exportSmoothgroups = getCheck(hDlg, IDC_CHK_EXPORT_SMOOTHGROUPS);
    opts.keepUnusedBonesHelpers = getCheck(hDlg, IDC_CHK_KEEP_UNUSED_BH);
    opts.disableSkinQuantize = getCheck(hDlg, IDC_CHK_DISABLE_SKINQUANT);

    // Threshold
    wchar_t threshText[32];
    GetDlgItemTextW(hDlg, IDC_EDT_THRESHOLD, threshText, 32);
    opts.fixNormalsThreshold = static_cast<float>(_wtof(threshText));

    // Extents
    opts.extentsType = getCheck(hDlg, IDC_RDO_EXTENTS_GLOBAL) ? 2 : 1;

    wchar_t precText[16];
    GetDlgItemTextW(hDlg, IDC_EDT_EXTENTS_PREC, precText, 16);
    opts.extentsPrecision = _wtoi(precText);
}

// ── Populate dialog from options struct ────────────────────

void optionsToDialog(HWND hDlg, const MdxExportOptions& opts) {
    // Model Name
    if (!opts.modelName.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, opts.modelName.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wname(static_cast<size_t>(wlen - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, opts.modelName.c_str(), -1, wname.data(), wlen);
            SetDlgItemTextW(hDlg, IDC_EDT_MODEL_NAME, wname.c_str());
        }
    }

    // Format Version
    setCheck(hDlg, IDC_RDO_CLASSIC, opts.version < 1200);
    setCheck(hDlg, IDC_RDO_REFORGED, opts.version >= 1200);

    // Options
    setCheck(hDlg, IDC_CHK_MERGE_SIMILAR, opts.mergeGeosets);
    setCheck(hDlg, IDC_CHK_FIX_NORMALS, opts.fixNormals);
    setCheck(hDlg, IDC_CHK_FIX_SHARED_NORMALS, opts.fixSharedNormals);
    setCheck(hDlg, IDC_CHK_EXPORT_SMOOTHGROUPS, opts.exportSmoothgroups);
    setCheck(hDlg, IDC_CHK_KEEP_UNUSED_BH, opts.keepUnusedBonesHelpers);
    setCheck(hDlg, IDC_CHK_DISABLE_SKINQUANT, opts.disableSkinQuantize);

    // Threshold
    wchar_t threshText[32];
    swprintf_s(threshText, L"%.3f", static_cast<double>(opts.fixNormalsThreshold));
    SetDlgItemTextW(hDlg, IDC_EDT_THRESHOLD, threshText);

    // Extents
    setCheck(hDlg, IDC_RDO_EXTENTS_ANIM, opts.extentsType != 2);
    setCheck(hDlg, IDC_RDO_EXTENTS_GLOBAL, opts.extentsType == 2);

    wchar_t precText[16];
    _itow_s(opts.extentsPrecision, precText, 10);
    SetDlgItemTextW(hDlg, IDC_EDT_EXTENTS_PREC, precText);
}

// Apply enabled/disabled state based on checkbox dependencies
void applyEnabledState(HWND hDlg) {
    bool fixNormals = getCheck(hDlg, IDC_CHK_FIX_NORMALS);
    enableCtrl(hDlg, IDC_LBL_THRESHOLD, fixNormals);
    enableCtrl(hDlg, IDC_EDT_THRESHOLD, fixNormals);
    enableCtrl(hDlg, IDC_SPIN_THRESHOLD, fixNormals);
}

} // anonymous namespace

// ============================================================================
// Dialog Procedure
// ============================================================================

static INT_PTR CALLBACK ExportDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DialogState* ds = reinterpret_cast<DialogState*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        ds = reinterpret_cast<DialogState*>(lParam);
        SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ds));

        // Set up spinners
        HWND hSpinThresh = GetDlgItem(hDlg, IDC_SPIN_THRESHOLD);
        SendMessageW(hSpinThresh, UDM_SETRANGE32, 0, 10000); // 0.000 - 100.000 (in thousandths)

        HWND hSpinPrec = GetDlgItem(hDlg, IDC_SPIN_EXTENTS_PREC);
        SendMessageW(hSpinPrec, UDM_SETRANGE32, 0, 10);

        // Populate controls from defaults, then overlay with INI
        optionsToDialog(hDlg, *ds->opts);
        loadDialogSettingsFromINI(hDlg);

        // Apply cascading enable state
        applyEnabledState(hDlg);

        // Center dialog on parent
        CenterWindow(hDlg, GetParent(hDlg));
        return TRUE;
    }

    case WM_COMMAND: {
        if (!ds) break;
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        switch (id) {
        case IDOK:
            dialogToOptions(hDlg, *ds->opts);
            saveDialogSettingsToINI(hDlg);
            ds->confirmed = true;
            EndDialog(hDlg, IDOK);
            return TRUE;

        case IDCANCEL:
            ds->confirmed = false;
            EndDialog(hDlg, IDCANCEL);
            return TRUE;

        case IDC_CHK_FIX_NORMALS:
            if (code == BN_CLICKED)
                applyEnabledState(hDlg);
            return TRUE;

        default:
            break;
        }
        break;
    }

    case WM_CLOSE:
        if (ds) ds->confirmed = false;
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }

    return FALSE;
}

// ============================================================================
// Public API
// ============================================================================

bool showExportDialog(HINSTANCE hInstance, HWND hWndParent, MdxExportOptions& opts)
{
    DialogState ds{};
    ds.opts = &opts;
    ds.confirmed = false;

    INT_PTR result = DialogBoxParamW(
        hInstance, MAKEINTRESOURCEW(IDD_EXPORT_OPTIONS),
        hWndParent, ExportDialogProc,
        reinterpret_cast<LPARAM>(&ds));

    return (result == IDOK) && ds.confirmed;
}
