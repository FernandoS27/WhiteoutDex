// MDLXExporter — Export options dialog implementation
#include "export_dialog.h"
#include "resource.h"
#include "scene_monitor.h"

#include <max.h>
#include <MaxDirectories.h>
#include <commctrl.h>
#include <shellapi.h>      // ShellExecuteW
#include <string>
#include <cstdio>

// After max.h: that header has opinions about windows.h, which this one
// includes.
#include "wdx_localization.h" // wdx::l10n::LocalizeDialog
#include "wdx_window_icon.h"  // wdx::ApplyWindowIcon

// Provided by dllmain.cpp — we need this to launch the Problem Details dialog
extern HINSTANCE GetDllInstance();

// ============================================================================
// Dialog state passed via LPARAM → GWLP_USERDATA
// ============================================================================
struct DialogState {
    MdxExportOptions* opts;
    bool confirmed;
    // Scene Status: tracks whether the most recent scan found problems,
    // used by the WM_DRAWITEM handler to color the status bar (green/red).
    bool sceneHasProblems = false;
};

// ============================================================================
// Scene name (shared with mdx_exporter_plugin.cpp — see export_dialog.h)
// ============================================================================

std::wstring currentSceneModelName() {
    auto* gi = GetCOREInterface();
    if (!gi) return {};

    // GetCurFilePath() is empty until the scene has been saved at least once.
    MSTR filePathStr = gi->GetCurFilePath();
    const MCHAR* filePath = filePathStr.data();
    if (!filePath || !filePath[0]) return {};

    std::wstring fp(filePath);

    size_t lastSlash = fp.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos)
        fp = fp.substr(lastSlash + 1);

    size_t dot = fp.rfind(L'.');
    if (dot != std::wstring::npos)
        fp = fp.substr(0, dot);

    return fp;
}

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

// Path to the editable prefix-path list (a separate INI so the user can
// hand-edit it in Notepad without risking the main settings file).
// Lives next to MDLXExporter.ini in plugcfg.
std::wstring getMatPathsINIPath() {
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
    return std::wstring(dir.data()) + L"\\MDLXMatPrefixPaths.ini";
}

// Forward declaration — ensureMatPathsINI is defined after the Win32 alias.
void ensureMatPathsINI();

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

// Make sure the prefix-path INI exists with the 4 default WC3 paths.
// Called the first time the dropdown or edit button is pressed.
void ensureMatPathsINI() {
    std::wstring iniPath = getMatPathsINIPath();
    if (GetFileAttributesW(iniPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        return;  // already exists, don't overwrite the user's edits

    Win32_WritePrivateProfileStringW(L"PathMenu", L"Count", L"4", iniPath.c_str());
    Win32_WritePrivateProfileStringW(L"PathMenu", L"Path1",
        L"Textures\\", iniPath.c_str());
    Win32_WritePrivateProfileStringW(L"PathMenu", L"Path2",
        L"war3mapImported\\", iniPath.c_str());
    Win32_WritePrivateProfileStringW(L"PathMenu", L"Path3",
        L"ReplaceableTextures\\Shadows\\", iniPath.c_str());
    Win32_WritePrivateProfileStringW(L"PathMenu", L"Path4",
        L"ReplaceableTextures\\Cliff\\", iniPath.c_str());
}

// Show the prefix-path dropdown menu next to the dropdown button. Each
// entry is read from the [PathMenu] section of MDLXMatPrefixPaths.ini.
// Selecting a menu item sets the prefix edit field. Last entry is "(Clear)".
void showPrefixPathDropdown(HWND hDlg, HWND hButton) {
    ensureMatPathsINI();
    std::wstring iniPath = getMatPathsINIPath();

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    auto countStr = iniGetString(iniPath.c_str(), L"PathMenu", L"Count");
    int count = _wtoi(countStr.c_str());
    if (count < 0) count = 0;
    if (count > 32) count = 32;  // sanity cap

    for (int i = 1; i <= count; ++i) {
        wchar_t key[32];
        swprintf_s(key, 32, L"Path%d", i);
        auto path = iniGetString(iniPath.c_str(), L"PathMenu", key);
        if (path.empty()) continue;
        AppendMenuW(hMenu, MF_STRING, 1000 + i, path.c_str());
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 1999, L"(Clear)");

    // Position the menu just below the button
    RECT rc;
    GetWindowRect(hButton, &rc);

    UINT cmd = TrackPopupMenu(hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        rc.left, rc.bottom, 0, hDlg, nullptr);
    DestroyMenu(hMenu);

    if (cmd == 1999) {
        SetDlgItemTextW(hDlg, IDC_EDT_MAT_PREFIX, L"");
    } else if (cmd >= 1001 && cmd <= (UINT)(1000 + count)) {
        wchar_t key[32];
        swprintf_s(key, 32, L"Path%d", (int)(cmd - 1000));
        auto path = iniGetString(iniPath.c_str(), L"PathMenu", key);
        SetDlgItemTextW(hDlg, IDC_EDT_MAT_PREFIX, path.c_str());
    }
}

// Open the prefix-path INI in Notepad so the user can add/remove entries.
void openPrefixPathInNotepad() {
    ensureMatPathsINI();
    std::wstring iniPath = getMatPathsINIPath();
    // ShellExecuteW with notepad.exe — same approach as NeoDex.
    ShellExecuteW(nullptr, L"open", L"notepad.exe",
                   iniPath.c_str(), nullptr, SW_SHOWNORMAL);
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
    writeB(L"FixSharedNormals", getCheck(hDlg, IDC_CHK_FIX_SHARED_NORMALS));
    writeB(L"KeepUnusedBonesHelpers", getCheck(hDlg, IDC_CHK_KEEP_UNUSED_BH));
    writeB(L"DisableSkinQuantize", getCheck(hDlg, IDC_CHK_DISABLE_SKINQUANT));
    writeB(L"AutoIncrementFilename", getCheck(hDlg, IDC_CHK_AUTO_INCREMENT));
    writeB(L"OpenFolderAfterExport", getCheck(hDlg, IDC_CHK_OPEN_FOLDER));

    // Texture conversion (shared)
    writeB(L"TexConvertEnabled",   getCheck(hDlg, IDC_CHK_TEX_CONVERT));
    writeB(L"TexGenerateMipmaps",  getCheck(hDlg, IDC_CHK_TEX_MIPMAPS));
    writeB(L"TexOverwriteExisting",getCheck(hDlg, IDC_CHK_TEX_OVERWRITE));

    // BLP-specific
    int blpComp = (int)SendMessageW(GetDlgItem(hDlg, IDC_CMB_BLP_COMPRESSION), CB_GETCURSEL, 0, 0);
    if (blpComp < 0) blpComp = 0;
    writeI(L"BlpCompression", blpComp);

    wchar_t jpegText[16];
    GetDlgItemTextW(hDlg, IDC_EDT_BLP_JPEG_QUALITY, jpegText, 16);
    writeS(L"BlpJpegQuality", jpegText);

    writeB(L"BlpDithering", getCheck(hDlg, IDC_CHK_BLP_DITHERING));

    // DDS-specific
    int ddsComp = (int)SendMessageW(GetDlgItem(hDlg, IDC_CMB_DDS_FORMAT), CB_GETCURSEL, 0, 0);
    if (ddsComp < 0) ddsComp = 0;
    writeI(L"DdsFormat", ddsComp);

    // Active tab (so we restore the user's last view)
    int lastTab = TabCtrl_GetCurSel(GetDlgItem(hDlg, IDC_TAB_CONTROL));
    writeI(L"LastTab", lastTab < 0 ? 0 : lastTab);

    // Extents — type is always animation-dependent, only precision is configurable
    wchar_t precText[16];
    GetDlgItemTextW(hDlg, IDC_EDT_EXTENTS_PREC, precText, 16);
    writeS(L"ExtentsPrecision", precText);

    // Material Fix settings — separate INI section so they're not mixed
    // with the export settings. Used by scene_monitor::fixUnsupportedMaterials.
    auto writeBM = [&](const wchar_t* key, bool val) {
        Win32_WritePrivateProfileStringW(L"MaterialFix", key,
            val ? L"true" : L"false", iniPath.c_str());
    };
    auto writeIM = [&](const wchar_t* key, int val) {
        wchar_t buf[16];
        _itow_s(val, buf, 10);
        Win32_WritePrivateProfileStringW(L"MaterialFix", key, buf, iniPath.c_str());
    };
    auto writeSM = [&](const wchar_t* key, const wchar_t* val) {
        Win32_WritePrivateProfileStringW(L"MaterialFix", key, val, iniPath.c_str());
    };

    writeBM(L"Unshaded", getCheck(hDlg, IDC_CHK_MAT_UNSHADED));
    writeBM(L"Unfogged", getCheck(hDlg, IDC_CHK_MAT_UNFOGGED));
    writeBM(L"Twosided", getCheck(hDlg, IDC_CHK_MAT_TWOSIDED));
    writeBM(L"UTile",    getCheck(hDlg, IDC_CHK_MAT_UTILE));
    writeBM(L"VTile",    getCheck(hDlg, IDC_CHK_MAT_VTILE));

    // Filter mode (1..7 to match NeoDex radio state)
    int filterMode = 1;
    if      (getCheck(hDlg, IDC_RDO_MAT_FILTER_NONE))   filterMode = 1;
    else if (getCheck(hDlg, IDC_RDO_MAT_FILTER_TRANSP)) filterMode = 2;
    else if (getCheck(hDlg, IDC_RDO_MAT_FILTER_BLEND))  filterMode = 3;
    else if (getCheck(hDlg, IDC_RDO_MAT_FILTER_ADD))    filterMode = 4;
    else if (getCheck(hDlg, IDC_RDO_MAT_FILTER_ADD2X))  filterMode = 5;
    else if (getCheck(hDlg, IDC_RDO_MAT_FILTER_MOD))    filterMode = 6;
    else if (getCheck(hDlg, IDC_RDO_MAT_FILTER_MOD2X))  filterMode = 7;
    writeIM(L"FilterMode", filterMode);

    wchar_t prefixText[260];
    GetDlgItemTextW(hDlg, IDC_EDT_MAT_PREFIX, prefixText, 260);
    writeSM(L"PrefixPath", prefixText);
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

    auto fixSN = gs(L"FixSharedNormals");
    if (!fixSN.empty()) setCheck(hDlg, IDC_CHK_FIX_SHARED_NORMALS, iniBool(fixSN));

    auto keepBH = gs(L"KeepUnusedBonesHelpers");
    if (!keepBH.empty()) setCheck(hDlg, IDC_CHK_KEEP_UNUSED_BH, iniBool(keepBH));

    auto noSkQ = gs(L"DisableSkinQuantize");
    if (!noSkQ.empty()) setCheck(hDlg, IDC_CHK_DISABLE_SKINQUANT, iniBool(noSkQ));

    auto autoI = gs(L"AutoIncrementFilename");
    if (!autoI.empty()) setCheck(hDlg, IDC_CHK_AUTO_INCREMENT, iniBool(autoI));

    auto openF = gs(L"OpenFolderAfterExport");
    if (!openF.empty()) setCheck(hDlg, IDC_CHK_OPEN_FOLDER, iniBool(openF));

    // Texture conversion (shared)
    auto tcConv = gs(L"TexConvertEnabled");
    if (!tcConv.empty()) setCheck(hDlg, IDC_CHK_TEX_CONVERT, iniBool(tcConv));

    auto tcMip = gs(L"TexGenerateMipmaps");
    if (!tcMip.empty()) setCheck(hDlg, IDC_CHK_TEX_MIPMAPS, iniBool(tcMip));

    auto tcOver = gs(L"TexOverwriteExisting");
    if (!tcOver.empty()) setCheck(hDlg, IDC_CHK_TEX_OVERWRITE, iniBool(tcOver));

    // BLP-specific
    auto blpComp = gs(L"BlpCompression");
    if (!blpComp.empty()) {
        int v = _wtoi(blpComp.c_str());
        SendMessageW(GetDlgItem(hDlg, IDC_CMB_BLP_COMPRESSION), CB_SETCURSEL, v, 0);
    }

    auto jpegQ = gs(L"BlpJpegQuality");
    if (!jpegQ.empty()) SetDlgItemTextW(hDlg, IDC_EDT_BLP_JPEG_QUALITY, jpegQ.c_str());

    auto blpDith = gs(L"BlpDithering");
    if (!blpDith.empty()) setCheck(hDlg, IDC_CHK_BLP_DITHERING, iniBool(blpDith));

    // DDS-specific
    auto ddsFmt = gs(L"DdsFormat");
    if (!ddsFmt.empty()) {
        int v = _wtoi(ddsFmt.c_str());
        SendMessageW(GetDlgItem(hDlg, IDC_CMB_DDS_FORMAT), CB_SETCURSEL, v, 0);
    }

    // Active tab — must run AFTER initTabControl populated the tabs
    auto lastTab = gs(L"LastTab");
    if (!lastTab.empty()) {
        int t = _wtoi(lastTab.c_str());
        if (t < 0) t = 0;
        if (t > 2) t = 2;  // 3 tabs: Options=0, Material Fix=1, Texture=2
        TabCtrl_SetCurSel(GetDlgItem(hDlg, IDC_TAB_CONTROL), t);
    }

    // Extents — type is always animation-dependent, only precision is configurable
    auto extPrec = gs(L"ExtentsPrecision");
    if (!extPrec.empty())
        SetDlgItemTextW(hDlg, IDC_EDT_EXTENTS_PREC, extPrec.c_str());

    // Material Fix settings — read from [MaterialFix] section
    auto gm = [&](const wchar_t* key) {
        return iniGetString(iniPath.c_str(), L"MaterialFix", key);
    };

    auto unshaded = gm(L"Unshaded");
    if (!unshaded.empty()) setCheck(hDlg, IDC_CHK_MAT_UNSHADED, iniBool(unshaded));
    auto unfogged = gm(L"Unfogged");
    if (!unfogged.empty()) setCheck(hDlg, IDC_CHK_MAT_UNFOGGED, iniBool(unfogged));
    auto twosided = gm(L"Twosided");
    if (!twosided.empty()) setCheck(hDlg, IDC_CHK_MAT_TWOSIDED, iniBool(twosided));
    auto utile = gm(L"UTile");
    if (!utile.empty()) setCheck(hDlg, IDC_CHK_MAT_UTILE, iniBool(utile));
    auto vtile = gm(L"VTile");
    if (!vtile.empty()) setCheck(hDlg, IDC_CHK_MAT_VTILE, iniBool(vtile));

    // Filter mode default = 1 (None) if not set
    int filterMode = 1;
    auto fm = gm(L"FilterMode");
    if (!fm.empty()) {
        filterMode = _wtoi(fm.c_str());
        if (filterMode < 1 || filterMode > 7) filterMode = 1;
    }
    setCheck(hDlg, IDC_RDO_MAT_FILTER_NONE,   filterMode == 1);
    setCheck(hDlg, IDC_RDO_MAT_FILTER_TRANSP, filterMode == 2);
    setCheck(hDlg, IDC_RDO_MAT_FILTER_BLEND,  filterMode == 3);
    setCheck(hDlg, IDC_RDO_MAT_FILTER_ADD,    filterMode == 4);
    setCheck(hDlg, IDC_RDO_MAT_FILTER_ADD2X,  filterMode == 5);
    setCheck(hDlg, IDC_RDO_MAT_FILTER_MOD,    filterMode == 6);
    setCheck(hDlg, IDC_RDO_MAT_FILTER_MOD2X,  filterMode == 7);

    auto prefix = gm(L"PrefixPath");
    if (!prefix.empty())
        SetDlgItemTextW(hDlg, IDC_EDT_MAT_PREFIX, prefix.c_str());
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

    // Format Version. Reforged is written as v1800, the version WC3 3.0.0 ships.
    opts.version = getCheck(hDlg, IDC_RDO_REFORGED) ? 1800 : 800;

    // Options
    opts.mergeGeosets = getCheck(hDlg, IDC_CHK_MERGE_SIMILAR);
    opts.fixSharedNormals = getCheck(hDlg, IDC_CHK_FIX_SHARED_NORMALS);
    opts.keepUnusedBonesHelpers = getCheck(hDlg, IDC_CHK_KEEP_UNUSED_BH);
    opts.disableSkinQuantize = getCheck(hDlg, IDC_CHK_DISABLE_SKINQUANT);
    opts.autoIncrementFilename = getCheck(hDlg, IDC_CHK_AUTO_INCREMENT);
    opts.openFolderAfterExport = getCheck(hDlg, IDC_CHK_OPEN_FOLDER);
    // exportSmoothgroups is forced true via struct default; no UI binding
    // extentsType is forced 1 (animation-dependent) via struct default; no UI binding

    // Texture conversion (shared)
    opts.texConvertEnabled    = getCheck(hDlg, IDC_CHK_TEX_CONVERT);
    opts.texGenerateMipmaps   = getCheck(hDlg, IDC_CHK_TEX_MIPMAPS);
    opts.texOverwriteExisting = getCheck(hDlg, IDC_CHK_TEX_OVERWRITE);

    // BLP-specific
    int blpComp = (int)SendMessageW(GetDlgItem(hDlg, IDC_CMB_BLP_COMPRESSION), CB_GETCURSEL, 0, 0);
    opts.blpCompression = (blpComp < 0) ? 0 : blpComp;

    wchar_t jpegText[16];
    GetDlgItemTextW(hDlg, IDC_EDT_BLP_JPEG_QUALITY, jpegText, 16);
    opts.blpJpegQuality = _wtoi(jpegText);

    opts.blpDithering = getCheck(hDlg, IDC_CHK_BLP_DITHERING);

    // DDS-specific
    int ddsComp = (int)SendMessageW(GetDlgItem(hDlg, IDC_CMB_DDS_FORMAT), CB_GETCURSEL, 0, 0);
    opts.ddsFormat = (ddsComp < 0) ? 0 : ddsComp;

    // Extents — type is always animation-dependent, only precision is configurable
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
    setCheck(hDlg, IDC_CHK_FIX_SHARED_NORMALS, opts.fixSharedNormals);
    setCheck(hDlg, IDC_CHK_KEEP_UNUSED_BH, opts.keepUnusedBonesHelpers);
    setCheck(hDlg, IDC_CHK_DISABLE_SKINQUANT, opts.disableSkinQuantize);
    setCheck(hDlg, IDC_CHK_AUTO_INCREMENT, opts.autoIncrementFilename);
    setCheck(hDlg, IDC_CHK_OPEN_FOLDER, opts.openFolderAfterExport);

    // Texture conversion (shared)
    setCheck(hDlg, IDC_CHK_TEX_CONVERT,   opts.texConvertEnabled);
    setCheck(hDlg, IDC_CHK_TEX_MIPMAPS,   opts.texGenerateMipmaps);
    setCheck(hDlg, IDC_CHK_TEX_OVERWRITE, opts.texOverwriteExisting);

    // BLP-specific
    SendMessageW(GetDlgItem(hDlg, IDC_CMB_BLP_COMPRESSION), CB_SETCURSEL, opts.blpCompression, 0);
    {
        wchar_t jpegText[16];
        _itow_s(opts.blpJpegQuality, jpegText, 10);
        SetDlgItemTextW(hDlg, IDC_EDT_BLP_JPEG_QUALITY, jpegText);
    }
    setCheck(hDlg, IDC_CHK_BLP_DITHERING, opts.blpDithering);

    // DDS-specific
    SendMessageW(GetDlgItem(hDlg, IDC_CMB_DDS_FORMAT), CB_SETCURSEL, opts.ddsFormat, 0);

    // Extents — type is always animation-dependent, only precision is configurable
    wchar_t precText[16];
    _itow_s(opts.extentsPrecision, precText, 10);
    SetDlgItemTextW(hDlg, IDC_EDT_EXTENTS_PREC, precText);
}

// ── Tab visibility management ──────────────────────────────────────────────
// The Options and Texture-Conversion controls are all dialog-level children
// that overlap the tab control's body area. We show/hide entire sets based
// on the active tab and (for the Texture tab) the selected Format Version.

static const int kOptionsTabControls[] = {
    IDC_CHK_AUTO_INCREMENT,
    IDC_CHK_MERGE_SIMILAR,
    IDC_CHK_FIX_SHARED_NORMALS,
    IDC_CHK_KEEP_UNUSED_BH,
    IDC_CHK_DISABLE_SKINQUANT,
    IDC_CHK_OPEN_FOLDER,
};

static const int kMaterialFixTabControls[] = {
    IDC_GRP_MAT_BASIC,
    IDC_CHK_MAT_UNSHADED,
    IDC_CHK_MAT_UNFOGGED,
    IDC_CHK_MAT_TWOSIDED,
    IDC_GRP_MAT_FILTER,
    IDC_RDO_MAT_FILTER_NONE,
    IDC_RDO_MAT_FILTER_TRANSP,
    IDC_RDO_MAT_FILTER_BLEND,
    IDC_RDO_MAT_FILTER_ADD,
    IDC_RDO_MAT_FILTER_ADD2X,
    IDC_RDO_MAT_FILTER_MOD,
    IDC_RDO_MAT_FILTER_MOD2X,
    IDC_GRP_MAT_TEXPARAMS,
    IDC_LBL_MAT_PREFIX,
    IDC_EDT_MAT_PREFIX,
    IDC_BTN_MAT_PREFIX_DROP,
    IDC_BTN_MAT_PREFIX_EDIT,
    IDC_CHK_MAT_UTILE,
    IDC_CHK_MAT_VTILE,
};

static const int kTextureSharedControls[] = {
    IDC_CHK_TEX_CONVERT,
    IDC_CHK_TEX_MIPMAPS,
    IDC_CHK_TEX_OVERWRITE,
};

static const int kBlpControls[] = {
    IDC_LBL_BLP_COMPRESSION,
    IDC_CMB_BLP_COMPRESSION,
    IDC_LBL_BLP_JPEG_QUALITY,
    IDC_EDT_BLP_JPEG_QUALITY,
    IDC_SPIN_BLP_JPEG_QUALITY,
    IDC_CHK_BLP_DITHERING,
};

static const int kDdsControls[] = {
    IDC_LBL_DDS_FORMAT,
    IDC_CMB_DDS_FORMAT,
};

template <size_t N>
void showSet(HWND hDlg, const int (&ids)[N], bool show) {
    int cmd = show ? SW_SHOW : SW_HIDE;
    for (size_t i = 0; i < N; ++i)
        ShowWindow(GetDlgItem(hDlg, ids[i]), cmd);
}

void applyTabAndFormatVisibility(HWND hDlg) {
    HWND hTab = GetDlgItem(hDlg, IDC_TAB_CONTROL);
    int  tab  = TabCtrl_GetCurSel(hTab);
    bool optionsActive  = (tab == 0);
    bool materialActive = (tab == 1);
    bool textureActive  = (tab == 2);
    bool isReforged     = getCheck(hDlg, IDC_RDO_REFORGED);

    showSet(hDlg, kOptionsTabControls,    optionsActive);
    showSet(hDlg, kMaterialFixTabControls, materialActive);
    showSet(hDlg, kTextureSharedControls,  textureActive);
    showSet(hDlg, kBlpControls, textureActive && !isReforged);
    showSet(hDlg, kDdsControls, textureActive &&  isReforged);
}

// ── Localization ───────────────────────────────────────────
//
// The .rc gives every control an English caption; this replaces them from the
// shared catalog (src/pre_startup_scripts/WhiteoutDexLocalization.ms) in one
// MaxScript round trip. A key the catalog does not carry leaves its control
// English, so the table may list controls whose translations are still pending.
//
// Not in the table: IDC_LBL_SCENE_STATUS, whose text the scene monitor rewrites
// as it runs and localizes where it sets it. Combo entries are handled by their
// own init functions below for the same reason.
//
// IDC_LBL_STATUS is in the table. Nothing ever rewrites it — the export loop
// reports through the progress bar — so its .rc caption "Idle" is what the user
// reads for the whole dialog's life, and it has to be translated here.

constexpr wdx::l10n::DialogString kExportStrings[] = {
    {0, "exp_export_settings_title"},

    {IDC_GRP_MODEL_NAME, "exp_model_name_grp"},
    {IDC_GRP_FORMAT_VERSION, "exp_format_version_grp"},

    // Tab 0 — Options
    {IDC_CHK_AUTO_INCREMENT, "exp_autoincrement_filename_chk"},
    {IDC_CHK_MERGE_SIMILAR, "exp_merge_similar_meshes_chk"},
    {IDC_CHK_FIX_SHARED_NORMALS, "exp_fix_shared_normals_chk"},
    {IDC_CHK_KEEP_UNUSED_BH, "exp_keep_unused_boneshelpers_chk"},
    {IDC_CHK_DISABLE_SKINQUANT, "exp_disable_skin_quantize_chk"},
    {IDC_CHK_OPEN_FOLDER, "exp_open_folder_after_export_chk"},

    // Tab 1 — Material Fix
    {IDC_GRP_MAT_BASIC, "exp_basic_parameters_grp"},
    {IDC_CHK_MAT_UNSHADED, "exp_unshaded_chk"},
    {IDC_CHK_MAT_UNFOGGED, "exp_unfogged_chk"},
    {IDC_CHK_MAT_TWOSIDED, "exp_two_sided_chk"},
    {IDC_GRP_MAT_FILTER, "exp_filter_mode_grp"},
    {IDC_GRP_MAT_TEXPARAMS, "exp_texture_parameters_grp"},
    {IDC_LBL_MAT_PREFIX, "exp_prefix_lbl"},
    {IDC_CHK_MAT_UTILE, "exp_utile_chk"},
    {IDC_CHK_MAT_VTILE, "exp_vtile_chk"},

    // Tab 2 — Texture Conversion
    {IDC_CHK_TEX_CONVERT, "exp_convert_textures_chk"},
    {IDC_LBL_BLP_COMPRESSION, "exp_compression_lbl"},
    {IDC_LBL_BLP_JPEG_QUALITY, "exp_jpeg_quality_lbl"},
    {IDC_CHK_BLP_DITHERING, "exp_dithering_chk"},
    {IDC_LBL_DDS_FORMAT, "exp_dds_format_lbl"},
    {IDC_CHK_TEX_MIPMAPS, "exp_regenerate_mipmaps_chk"},
    {IDC_CHK_TEX_OVERWRITE, "exp_overwrite_existing_chk"},

    {IDC_GRP_EXTENTS, "exp_extents_grp"},
    {IDC_LBL_EXTENTS_PREC, "exp_precision_lbl"},

    {IDC_GRP_SCENE_STATUS, "exp_scene_status_grp"},
    {IDC_BTN_SCENE_FIX_ALL, "exp_fix_all_btn"},
    {IDC_BTN_SCENE_DETAILS, "exp_details_btn"},

    {IDC_GRP_PROGRESS, "exp_progress_grp"},
    {IDC_LBL_STATUS, "exp_idle_lbl"},

    {IDOK, "exp_export_btn"},
    {IDCANCEL, "common_cancel_btn"},
};

// Shares its catalog entry with the MaxScript material UI, which reads the same
// pipe-joined string through WdxL.tList.
constexpr int kVersionIds[] = {IDC_RDO_CLASSIC, IDC_RDO_REFORGED};
constexpr int kFilterIds[] = {
    IDC_RDO_MAT_FILTER_NONE,  IDC_RDO_MAT_FILTER_TRANSP, IDC_RDO_MAT_FILTER_BLEND,
    IDC_RDO_MAT_FILTER_ADD,   IDC_RDO_MAT_FILTER_ADD2X,  IDC_RDO_MAT_FILTER_MOD,
    IDC_RDO_MAT_FILTER_MOD2X,
};

void localizeDialog(HWND hDlg) {
    wdx::l10n::LocalizeDialog(hDlg, kExportStrings);
    wdx::l10n::LocalizeRadioGroup(hDlg, "exp_version_labels", kVersionIds);
    wdx::l10n::LocalizeRadioGroup(hDlg, "exp_filter_mode_labels", kFilterIds);
}

void initTabControl(HWND hDlg) {
    HWND hTab = GetDlgItem(hDlg, IDC_TAB_CONTROL);
    TCITEMW tie = {};
    tie.mask = TCIF_TEXT;
    // TCITEM.pszText is not copied until InsertItem runs, so the strings
    // have to outlive each call — hence named locals rather than temporaries.
    const std::wstring tabs[] = {
        wdx::l10n::Tr("exp_options_tab"),
        wdx::l10n::Tr("exp_material_fix_tab"),
        wdx::l10n::Tr("exp_texture_conversion_tab"),
    };
    const wchar_t* fallback[] = {L"Options", L"Material Fix", L"Texture Conversion"};
    for (int i = 0; i < 3; ++i) {
        tie.pszText = const_cast<LPWSTR>(tabs[i].empty() ? fallback[i] : tabs[i].c_str());
        TabCtrl_InsertItem(hTab, i, &tie);
    }
    TabCtrl_SetCurSel(hTab, 0);
}

void initBlpCompressionCombo(HWND hDlg) {
    HWND hCmb = GetDlgItem(hDlg, IDC_CMB_BLP_COMPRESSION);
    SendMessageW(hCmb, CB_RESETCONTENT, 0, 0);
    // "JPEG" is a format name and stays; only the paletted entry has prose.
    const std::wstring paletted = wdx::l10n::Tr("exp_blp_paletted_item");
    SendMessageW(hCmb, CB_ADDSTRING, 0,
                 (LPARAM)(paletted.empty() ? L"Paletted (256 colors)" : paletted.c_str()));
    SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)L"JPEG");
    SendMessageW(hCmb, CB_SETCURSEL, 0, 0);
}

void initDdsFormatCombo(HWND hDlg) {
    HWND hCmb = GetDlgItem(hDlg, IDC_CMB_DDS_FORMAT);
    SendMessageW(hCmb, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)L"BC3 (DXT5)");
    SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)L"BC7");
    SendMessageW(hCmb, CB_SETCURSEL, 0, 0);
}

// Dithering only makes sense for paletted BLP. Disable + uncheck for JPEG.
void applyTextureEnabledState(HWND hDlg) {
    int blpComp = (int)SendMessageW(GetDlgItem(hDlg, IDC_CMB_BLP_COMPRESSION),
                                     CB_GETCURSEL, 0, 0);
    bool palettedActive = (blpComp == 0);
    enableCtrl(hDlg, IDC_CHK_BLP_DITHERING, palettedActive);
    if (!palettedActive) {
        // Force-uncheck so the saved opts don't carry a stale dithering flag
        // for a JPEG export.
        setCheck(hDlg, IDC_CHK_BLP_DITHERING, false);
    }
}

// Run a scan and update the Scene Status panel inside the export dialog.
// Re-enables Fix-All / Details buttons depending on whether problems exist.
// Called on dialog open and after the user runs Fix All or returns from
// the details dialog. Updates ds->sceneHasProblems and forces a redraw of
// the colored status bar.
void updateSceneStatusUI(HWND hDlg, DialogState* ds) {
    auto result = scene_monitor::scanScene();
    bool hasProblems = !result.empty();

    if (ds) ds->sceneHasProblems = hasProblems;

    HWND hLbl = GetDlgItem(hDlg, IDC_LBL_SCENE_STATUS);
    if (hLbl) {
        if (!hasProblems) {
            const std::wstring clear = wdx::l10n::Tr("exp_all_clear_lbl");
            SetWindowTextW(hLbl, clear.empty() ? L"All Clear!" : clear.c_str());
        } else {
            // Separate singular and plural entries rather than an English "%s"
            // suffix: most of the languages here do not pluralise with a
            // trailing letter, and several do not pluralise the noun at all.
            const std::wstring fmt =
                wdx::l10n::Tr(result.count() == 1 ? "exp_one_problem_fmt"
                                                  : "exp_n_problems_fmt");
            // A translation that lost its %d would read a garbage argument.
            const bool usable = fmt.find(L"%d") != std::wstring::npos;
            wchar_t buf[128];
            swprintf_s(buf, 128,
                       usable ? fmt.c_str()
                              : (result.count() == 1 ? L"%d problem found" : L"%d problems found"),
                       result.count());
            SetWindowTextW(hLbl, buf);
        }
    }

    // Force the colored bar to repaint with the new status color
    HWND hBar = GetDlgItem(hDlg, IDC_PNL_SCENE_STATUS_BAR);
    if (hBar) {
        InvalidateRect(hBar, nullptr, TRUE);
        UpdateWindow(hBar);
    }

    enableCtrl(hDlg, IDC_BTN_SCENE_FIX_ALL, hasProblems);
    enableCtrl(hDlg, IDC_BTN_SCENE_DETAILS, hasProblems);
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

        wdx::ApplyWindowIcon(hDlg, IDI_WHITEOUTDEX_ICON);

        // Relabel from the catalog before anything reads a caption back.
        localizeDialog(hDlg);

        // Set up spinners
        HWND hSpinPrec = GetDlgItem(hDlg, IDC_SPIN_EXTENTS_PREC);
        SendMessageW(hSpinPrec, UDM_SETRANGE32, 0, 10);

        HWND hSpinJpeg = GetDlgItem(hDlg, IDC_SPIN_BLP_JPEG_QUALITY);
        SendMessageW(hSpinJpeg, UDM_SETRANGE32, 1, 100);

        // Tab control + combo box population
        initTabControl(hDlg);
        initBlpCompressionCombo(hDlg);
        initDdsFormatCombo(hDlg);

        // Populate controls from defaults, then overlay with INI
        optionsToDialog(hDlg, *ds->opts);
        loadDialogSettingsFromINI(hDlg);

        // Model Name follows the scene: a saved scene always seeds the field
        // with its own name, overriding whatever the INI restored a moment ago.
        // The persisted value is per-user, not per-scene, so leaving it in
        // place stamped the previously exported model's name onto every later
        // export. The user can still type over it for this one export.
        // An unsaved scene keeps the INI value (there is nothing better).
        if (std::wstring sceneName = currentSceneModelName(); !sceneName.empty())
            SetDlgItemTextW(hDlg, IDC_EDT_MODEL_NAME, sceneName.c_str());

        // Apply initial visibility based on tab + format version,
        // then cascade enabled state (dithering depends on BLP compression).
        applyTabAndFormatVisibility(hDlg);
        applyTextureEnabledState(hDlg);

        // Run an initial scene scan to populate the status panel.
        // Cheap enough to do on dialog open — only scans the scene once.
        updateSceneStatusUI(hDlg, ds);

        // Center dialog on parent
        CenterWindow(hDlg, GetParent(hDlg));
        return TRUE;
    }

    case WM_COMMAND: {
        if (!ds) break;
        int id   = LOWORD(wParam);
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

        case IDC_RDO_CLASSIC:
        case IDC_RDO_REFORGED:
            // Switch BLP/DDS sub-block when format version changes
            applyTabAndFormatVisibility(hDlg);
            return TRUE;

        case IDC_CMB_BLP_COMPRESSION:
            if (code == CBN_SELCHANGE)
                applyTextureEnabledState(hDlg);
            return TRUE;

        case IDC_BTN_SCENE_FIX_ALL: {
            // Run all four fixers, then refresh the status panel.
            auto result = scene_monitor::scanScene();
            scene_monitor::fixAll(result);
            updateSceneStatusUI(hDlg, ds);
            return TRUE;
        }

        case IDC_BTN_SCENE_DETAILS: {
            // Show the modal Problem Details dialog.
            // It re-scans internally on every fix and on close.
            scene_monitor::showProblemDetailsDialog(GetDllInstance(), hDlg);
            // After it closes, update our own status panel
            updateSceneStatusUI(hDlg, ds);
            return TRUE;
        }

        case IDC_BTN_MAT_PREFIX_DROP: {
            // Show the dropdown menu of saved prefix paths.
            HWND hBtn = GetDlgItem(hDlg, IDC_BTN_MAT_PREFIX_DROP);
            showPrefixPathDropdown(hDlg, hBtn);
            return TRUE;
        }

        case IDC_BTN_MAT_PREFIX_EDIT: {
            // Open the prefix-path INI in Notepad so the user can add custom
            // entries. After they save and close, the next dropdown press
            // will pick up the changes.
            openPrefixPathInNotepad();
            return TRUE;
        }

        default:
            break;
        }
        break;
    }

    case WM_NOTIFY: {
        if (!ds) break;
        LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lParam);
        if (hdr && hdr->idFrom == IDC_TAB_CONTROL && hdr->code == TCN_SELCHANGE) {
            applyTabAndFormatVisibility(hDlg);
            return TRUE;
        }
        break;
    }

    // Owner-draw the Scene Status colored bar. The control is created with
    // SS_OWNERDRAW so the system sends WM_DRAWITEM whenever it needs paint.
    // Color: green (RGB 76,175,80) if scene is clean, red (RGB 220,53,69)
    // if problems were detected. Matches the NeoDex visual convention.
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis && dis->CtlID == IDC_PNL_SCENE_STATUS_BAR) {
            COLORREF fillColor = (ds && ds->sceneHasProblems)
                ? RGB(220, 53, 69)   // red
                : RGB( 76, 175, 80); // green
            HBRUSH hBrush = CreateSolidBrush(fillColor);
            FillRect(dis->hDC, &dis->rcItem, hBrush);
            DeleteObject(hBrush);
            return TRUE;
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
