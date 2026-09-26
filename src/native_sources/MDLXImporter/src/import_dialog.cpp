// MDLXImporter — Import options dialog implementation
//
// Redesign, September 2026 (same look as the export dialog):
//   header band with the file, its version and what it contains,
//   Fast Settings drop-down, Mode cards, three tabs (Objects, Animations,
//   Model) and a footer with the texture archive status.
//
// Behaviour:
//   - Every checkbox is always usable. As soon as the checkboxes no longer
//     match the chosen Fast Setting, the drop-down switches to Custom.
//   - A category the file does not contain (count 0) is unchecked and
//     disabled; its saved INI value is left untouched.
//   - The preset rules are the ones onFastSettingsChanged used before, kept in
//     one table (presetValues) that both applies and compares presets.
//   - After Import the window stays open and its footer shows the progress
//     of the import (ImportDialog::step / items) until DoImport closes it.
//
// Layout values are pixels at 96 DPI, scaled by the dialog's DPI at runtime.
// The .rc only defines controls, styles and tab order. Plain Win32 plus GDI+
// for anti-aliased shapes, so one code path serves 3ds Max 2016 through 2027.
#include "import_dialog.h"
#include "resource.h"

#include <max.h>
#include <MaxDirectories.h>
#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

// GDI+ uses min/max unqualified. The build defines NOMINMAX, so hand it the
// std versions; without NOMINMAX the windows.h macros already cover it.
#ifndef min
namespace Gdiplus { using std::min; using std::max; }
#endif
#include <objidl.h>   // IStream, needed by gdiplus.h under WIN32_LEAN_AND_MEAN
#include <gdiplus.h>

// After max.h: that header has opinions about windows.h, which this one
// includes.
#include "wdx_localization.h" // wdx::l10n::LocalizeDialog
#include "wdx_window_icon.h"  // wdx::ApplyWindowIcon

#ifdef _MSC_VER
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#endif

// Older Windows SDKs (Max 2016-era toolsets) do not declare these.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

namespace {

// ============================================================================
// Design tokens (same values as the export dialog)
// ============================================================================

namespace col {
constexpr COLORREF body         = RGB(0x44, 0x44, 0x44);
constexpr COLORREF band         = RGB(0x3A, 0x3A, 0x3A);
constexpr COLORREF bandLine     = RGB(0x33, 0x33, 0x33);
constexpr COLORREF text         = RGB(0xDC, 0xDC, 0xDC);
constexpr COLORREF textDim      = RGB(0xB4, 0xB4, 0xB4);
constexpr COLORREF textBright   = RGB(0xF2, 0xF2, 0xF2);
constexpr COLORREF textWarn     = RGB(0xF2, 0xC2, 0x6B);
constexpr COLORREF textDisabled = RGB(0x8C, 0x8C, 0x8C);
constexpr COLORREF editBg       = RGB(0x38, 0x38, 0x38);
constexpr COLORREF editText     = RGB(0xF2, 0xF2, 0xF2);
constexpr COLORREF cardBg       = RGB(0x4A, 0x4A, 0x4A);
constexpr COLORREF cardBorder   = RGB(0x5A, 0x5A, 0x5A);
constexpr COLORREF cardHover    = RGB(0x6E, 0x6E, 0x6E);
constexpr COLORREF cardSelBg    = RGB(0x3B, 0x49, 0x55);
constexpr COLORREF cardTitle    = RGB(0xF0, 0xF0, 0xF0);
constexpr COLORREF cardSub      = RGB(0xC8, 0xC8, 0xC8);
constexpr COLORREF radioRing    = RGB(0x8A, 0x8A, 0x8A);
constexpr COLORREF accent       = RGB(0x4C, 0xA3, 0xE6);
constexpr COLORREF tabText      = RGB(0xB4, 0xB4, 0xB4);
constexpr COLORREF tabSelected  = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF tabHover     = RGB(0xE8, 0xE8, 0xE8);
constexpr COLORREF tabLine      = RGB(0x57, 0x57, 0x57);
constexpr COLORREF badgeReforged= RGB(0xBF, 0xE0, 0xF7);
constexpr COLORREF badgeClassic = RGB(0xCF, 0xCF, 0xCF);
constexpr COLORREF badgeClassicBorder = RGB(0x7D, 0x7D, 0x7D);
constexpr COLORREF primary      = RGB(0x00, 0x78, 0xD7);
constexpr COLORREF primaryHover = RGB(0x1A, 0x86, 0xDD);
constexpr COLORREF primaryDown  = RGB(0x00, 0x67, 0xB8);
constexpr COLORREF primaryText  = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF focus        = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF docPage      = RGB(0xD9, 0xD9, 0xD9);
constexpr COLORREF docFold      = RGB(0xA8, 0xA8, 0xA8);
constexpr COLORREF docLines     = RGB(0x6B, 0x6B, 0x6B);
} // namespace col

// ============================================================================
// Layout: client area 422 px wide at 96 DPI; the height follows the tallest
// tab page of this file (Classic 447 px, Reforged 451 px).
// ============================================================================

struct Px { int x, y, w, h; };

namespace lay {
constexpr int clientW = 422;
constexpr int right = 411;              // right edge of the content

constexpr int headerH = 54;             // band 0..53, separator row at y = 53
constexpr int headerLineY = 53;
constexpr Px  docIcon{11, 10, 32, 32};
constexpr int nameX = 55, nameY = 9, lineY = 27, textH = 16;
constexpr int badgeGap = 7, badgePadX = 6, badgeH = 16;

constexpr Px  lblFast{12, 66, 398, 16};
constexpr Px  cmbFast{12, 87, 398, 23};
constexpr Px  lblMode{12, 121, 398, 16};
constexpr Px  cardNew{12, 142, 196, 42};
constexpr Px  cardMerge{215, 142, 196, 42};

constexpr int tabX = 12, tabY = 195, tabH = 24, tabGap = 22;
constexpr int underlineY = 219, underlineH = 2;
constexpr Px  tabStrip{12, 220, 398, 1};

constexpr int pageX = 12, pageY = 232, pageW = 398;
constexpr int rowH = 17, rowPitch = 24, rowGap = 7;
constexpr int subX = 31;                // indented rows under a master checkbox
constexpr int colW0 = 181, colGap = 16, colW1 = 182;
constexpr int countW = 28, countGap = 6;
constexpr int noteH = 16;

// Model tab
constexpr int optionsLabelY = pageY + 74;     // 3 rows, then the "Options" label
constexpr int optionsFirstRowY = pageY + 92;

constexpr int footerPad = 12;           // gap between page and footer band
constexpr int footerH = 46;             // 1 px line + 11 + 23 + 11
constexpr int progTextY = 7;            // progress row, from the footer's top
constexpr int progBarY = 29, progBarH = 6;
constexpr int btnH = 23, btnGap = 7, btnMinW = 75, btnPad = 12;
constexpr int comboDropH = 160;
} // namespace lay

// ============================================================================
// Control tables
// ============================================================================

// Object rows in reading order (two columns, row by row).
constexpr int kObjectRows[] = {
    IDC_CHK_BONES,         IDC_CHK_HELPERS,
    IDC_CHK_LIGHTS,        IDC_CHK_ATTACHMENTS,
    IDC_CHK_EVENT_OBJECTS, IDC_CHK_PE1,
    IDC_CHK_PE2,           IDC_CHK_RIBBON_EMITTERS,
    IDC_CHK_COLLISION_SHAPES, IDC_CHK_CAMERAS,
    IDC_CHK_CORN_EMITTERS, IDC_CHK_FACEFX,
};

constexpr int kAnimRows[] = {
    IDC_CHK_TRANSLATION, IDC_CHK_ROTATIONS,
    IDC_CHK_SCALE,       IDC_CHK_VISIBILITY,
    IDC_CHK_PARAMETER,   IDC_CHK_UNWRAP_ANIMS,
    IDC_CHK_TEXTURE_ANIMS, IDC_CHK_COLOR,
};

constexpr int kModelRows[] = {
    IDC_CHK_SKINNED, IDC_CHK_IMPORT_MATERIALS, IDC_CHK_IMPORT_TEXTURES,
};

constexpr int kOptionRows[] = {
    IDC_CHK_POINT_HELPERS, IDC_CHK_OPT_GEOMETRY, IDC_CHK_OPT_BONES,
};

// Every checkbox a Fast Setting decides (same set as before the redesign).
constexpr int kFastIds[] = {
    IDC_CHK_SKINNED, IDC_CHK_IMPORT_MATERIALS, IDC_CHK_IMPORT_TEXTURES,
    IDC_CHK_IMPORT_OBJECTS, IDC_CHK_BONES, IDC_CHK_HELPERS, IDC_CHK_LIGHTS,
    IDC_CHK_ATTACHMENTS, IDC_CHK_PE1, IDC_CHK_PE2, IDC_CHK_EVENT_OBJECTS,
    IDC_CHK_RIBBON_EMITTERS, IDC_CHK_COLLISION_SHAPES, IDC_CHK_CAMERAS,
    IDC_CHK_CORN_EMITTERS, IDC_CHK_FACEFX,
    IDC_CHK_IMPORT_ANIMATIONS, IDC_CHK_TRANSLATION, IDC_CHK_ROTATIONS,
    IDC_CHK_SCALE, IDC_CHK_PARAMETER, IDC_CHK_UNWRAP_ANIMS,
    IDC_CHK_TEXTURE_ANIMS, IDC_CHK_VISIBILITY, IDC_CHK_COLOR,
};

// Checkboxes that show a count on their right.
constexpr int kCountedIds[] = {
    IDC_CHK_IMPORT_MATERIALS, IDC_CHK_IMPORT_TEXTURES,
    IDC_CHK_BONES, IDC_CHK_HELPERS, IDC_CHK_LIGHTS, IDC_CHK_ATTACHMENTS,
    IDC_CHK_PE1, IDC_CHK_PE2, IDC_CHK_EVENT_OBJECTS, IDC_CHK_RIBBON_EMITTERS,
    IDC_CHK_COLLISION_SHAPES, IDC_CHK_CAMERAS, IDC_CHK_CORN_EMITTERS,
    IDC_CHK_FACEFX, IDC_CHK_IMPORT_ANIMATIONS,
};

constexpr int kOwnerDrawn[] = {
    IDC_BTN_MODE_NEW_SCENE, IDC_BTN_MODE_MERGE,
    IDC_BTN_TAB_OBJECTS, IDC_BTN_TAB_ANIMATIONS, IDC_BTN_TAB_MODEL, IDOK,
};

constexpr int kTabIds[3] = {IDC_BTN_TAB_OBJECTS, IDC_BTN_TAB_ANIMATIONS, IDC_BTN_TAB_MODEL};

// Checkbox <-> option field <-> INI key (INI keys unchanged).
struct CoreRow {
    int id;
    bool ir::CoreImportOptions::*field;
    const wchar_t* iniKey;
};

const CoreRow kCoreRows[] = {
    {IDC_CHK_SKINNED,            &ir::CoreImportOptions::importSkinning,           L"ImportSkinning"},
    {IDC_CHK_IMPORT_MATERIALS,   &ir::CoreImportOptions::importMaterials,          L"ImportMaterials"},
    {IDC_CHK_IMPORT_TEXTURES,    &ir::CoreImportOptions::importTextures,           L"ImportTextures"},
    {IDC_CHK_IMPORT_OBJECTS,     &ir::CoreImportOptions::importObjects,            L"ImportObjects"},
    {IDC_CHK_BONES,              &ir::CoreImportOptions::importBones,              L"ImportBones"},
    {IDC_CHK_HELPERS,            &ir::CoreImportOptions::importHelpers,            L"ImportHelpers"},
    {IDC_CHK_LIGHTS,             &ir::CoreImportOptions::importLights,             L"ImportLights"},
    {IDC_CHK_ATTACHMENTS,        &ir::CoreImportOptions::importAttachments,        L"ImportAttachments"},
    {IDC_CHK_PE1,                &ir::CoreImportOptions::importParticleEmitters1,  L"ImportParticleEmitters1"},
    {IDC_CHK_PE2,                &ir::CoreImportOptions::importParticleEmitters2,  L"ImportParticleEmitters2"},
    {IDC_CHK_RIBBON_EMITTERS,    &ir::CoreImportOptions::importRibbonEmitters,     L"ImportRibbonEmitters"},
    {IDC_CHK_EVENT_OBJECTS,      &ir::CoreImportOptions::importEventObjects,       L"ImportEventObjects"},
    {IDC_CHK_COLLISION_SHAPES,   &ir::CoreImportOptions::importCollisionShapes,    L"ImportCollisionShapes"},
    {IDC_CHK_CAMERAS,            &ir::CoreImportOptions::importCameras,            L"ImportCameras"},
    {IDC_CHK_IMPORT_ANIMATIONS,  &ir::CoreImportOptions::importAnimations,         L"ImportAnimations"},
    {IDC_CHK_TRANSLATION,        &ir::CoreImportOptions::importTranslation,        L"ImportTranslation"},
    {IDC_CHK_ROTATIONS,          &ir::CoreImportOptions::importRotation,           L"ImportRotation"},
    {IDC_CHK_SCALE,              &ir::CoreImportOptions::importScale,              L"ImportScale"},
    {IDC_CHK_PARAMETER,          &ir::CoreImportOptions::importParameterAnimations, L"ImportParameterAnimations"},
    {IDC_CHK_UNWRAP_ANIMS,       &ir::CoreImportOptions::importUVAnimations,       L"ImportUVAnimations"},
    {IDC_CHK_TEXTURE_ANIMS,      &ir::CoreImportOptions::importTextureAnimations,  L"ImportTextureAnimations"},
    {IDC_CHK_VISIBILITY,         &ir::CoreImportOptions::importVisibility,         L"ImportVisibility"},
    {IDC_CHK_COLOR,              &ir::CoreImportOptions::importColorAnimations,    L"ImportColorAnimations"},
    {IDC_CHK_POINT_HELPERS,      &ir::CoreImportOptions::importHelpersAsPointHelpers, L"ImportHelpersAsPointHelpers"},
    {IDC_CHK_OPT_GEOMETRY,       &ir::CoreImportOptions::optimizeGeometry,         L"OptimizeGeometry"},
    {IDC_CHK_OPT_BONES,          &ir::CoreImportOptions::optimizeBonesAndHelpers,  L"OptimizeBonesAndHelpers"},
};

// ============================================================================
// Import steps shown in the footer while the import runs
// ============================================================================

// Text and share of the bar (from, to in 0..1) of each ImportStep, in enum
// order. The shares follow what takes long on a typical animated model: the
// animation keys, then meshes and materials.
struct StepInfo {
    const char* key;
    const wchar_t* english;
    float from, to;
};

constexpr StepInfo kSteps[] = {
    {"imp_progress_preparing_lbl",  L"Preparing the model…",                        0.00f, 0.05f},
    {"imp_progress_skeleton_lbl",   L"Creating bones and helpers…",                 0.05f, 0.15f},
    {"imp_progress_meshes_lbl",     L"Creating meshes…",                            0.15f, 0.30f},
    {"imp_progress_textures_lbl",   L"Loading textures…",                           0.30f, 0.40f},
    {"imp_progress_materials_lbl",  L"Creating materials…",                         0.40f, 0.50f},
    {"imp_progress_objects_lbl",    L"Creating lights, emitters and attachments…",  0.50f, 0.58f},
    {"imp_progress_skin_lbl",       L"Applying skin…",                              0.58f, 0.65f},
    {"imp_progress_animations_lbl", L"Importing animations…",                       0.65f, 0.95f},
    {"imp_progress_finishing_lbl",  L"Finishing up…",                               0.95f, 1.00f},
};
static_assert(std::size(kSteps) == static_cast<size_t>(ImportStep::Count),
              "one kSteps entry per ImportStep");

// ============================================================================
// Dialog state passed via LPARAM → GWLP_USERDATA
// ============================================================================

struct DialogState {
    MdlxImportOptions* opts = nullptr;
    const ImportFileInfo* file = nullptr;
    bool confirmed = false;
    bool done = false;       // Import or Cancel chosen: ImportDialog::run leaves its loop
    bool updating = false;   // re-entrancy guard while code changes checkboxes

    // Progress, after Import: the footer shows it while the import runs.
    bool busy = false;
    std::vector<std::wstring> stepText;   // per ImportStep (catalog, English fallback)
    int    step = -1;
    size_t itemsDone = 0, itemsTotal = 0;
    float  progress = 0.0f;               // 0..1 of the whole import
    ULONGLONG lastPaint = 0;              // GetTickCount64 of the last footer repaint

    bool merge = false;      // Mode card
    int  tab = 0;            // 0 Objects, 1 Animations, 2 Model
    int  preset = 5;         // index into the drop-down = ir::CoreImportOptions::Preset

    // Header / footer text
    std::wstring headerName, headerBadge, headerLine, footerText;
    bool footerWarn = false;

    // Device-pixel positions the dialog paints around
    RECT tabRect[3]{};
    int  pageH = 0;          // 96-DPI pixels
    int  footerY = 0;        // 96-DPI pixels
    int  importX = 0;        // device pixels

    // Resources
    int    dpi = 96;
    HFONT  font = nullptr, fontSemibold = nullptr, fontSmall = nullptr;
    HBRUSH brBody = nullptr, brBand = nullptr, brEdit = nullptr;

    // Drawn strings (catalog, English fallback)
    std::wstring subNewScene, subMerge;

    int   S(int v) const { return MulDiv(v, dpi, 96); }
    float Sf(float v) const { return v * static_cast<float>(dpi) / 96.0f; }
    RECT  R(const Px& p) const { return RECT{S(p.x), S(p.y), S(p.x + p.w), S(p.y + p.h)}; }
};

bool g_gdiplus = false;  // GDI+ started by ImportDialog::run while the window exists

// ============================================================================
// Small helpers
// ============================================================================

void setCheck(HWND hDlg, int id, bool val) {
    CheckDlgButton(hDlg, id, val ? BST_CHECKED : BST_UNCHECKED);
}

bool getCheck(HWND hDlg, int id) {
    return IsDlgButtonChecked(hDlg, id) == BST_CHECKED;
}

void enableCtrl(HWND hDlg, int id, bool on) {
    EnableWindow(GetDlgItem(hDlg, id), on ? TRUE : FALSE);
}

// Checks the WS_VISIBLE style, not IsWindowVisible: during WM_INITDIALOG the
// dialog itself is still hidden, so IsWindowVisible reports every control as
// invisible and hiding would be skipped.
void showCtrl(HWND hDlg, int id, bool show) {
    HWND h = GetDlgItem(hDlg, id);
    if (!h) return;
    const bool visible = (GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE) != 0;
    if (visible != show)
        ShowWindow(h, show ? SW_SHOWNA : SW_HIDE);
}

void placeDevice(HWND hDlg, int id, int x, int y, int w, int h) {
    if (HWND c = GetDlgItem(hDlg, id))
        SetWindowPos(c, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void place(HWND hDlg, const DialogState* ds, int id, int x, int y, int w, int h) {
    placeDevice(hDlg, id, ds->S(x), ds->S(y), ds->S(x + w) - ds->S(x), ds->S(y + h) - ds->S(y));
}

// A drop-down list's window height is its selection field; the height passed
// to SetWindowPos also sizes the list. Set the field to |fieldH| and centre it
// if the control rounds the height.
void placeCombo(HWND hDlg, const DialogState* ds, int id, int x, int y, int w, int fieldH) {
    HWND h = GetDlgItem(hDlg, id);
    if (!h) return;
    SendMessageW(h, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), std::max(1, fieldH - 6));
    SetWindowPos(h, nullptr, x, y, w, fieldH + ds->S(lay::comboDropH), SWP_NOZORDER | SWP_NOACTIVATE);
    RECT wr{};
    GetWindowRect(h, &wr);
    const int actual = wr.bottom - wr.top;
    if (actual > 0 && actual != fieldH)
        SetWindowPos(h, nullptr, x, y + (fieldH - actual) / 2, w, actual + ds->S(lay::comboDropH),
                     SWP_NOZORDER | SWP_NOACTIVATE);
}

std::wstring windowText(HWND h) {
    const int len = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(std::max(0, len)) + 1, L'\0');
    GetWindowTextW(h, &s[0], len + 1);
    s.resize(static_cast<size_t>(std::max(0, len)));
    return s;
}

int textWidth(HWND hDlg, HFONT font, const std::wstring& s) {
    HDC dc = GetDC(hDlg);
    HGDIOBJ old = SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &sz);
    SelectObject(dc, old);
    ReleaseDC(hDlg, dc);
    return sz.cx;
}

void fillRectColor(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void frameRectColor(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FrameRect(dc, &r, b);
    DeleteObject(b);
}

RECT inset(RECT r, int d) {
    InflateRect(&r, -d, -d);
    return r;
}

void drawTextIn(HDC dc, const std::wstring& s, RECT r, HFONT font, COLORREF color, UINT fmt) {
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, fmt | DT_NOPREFIX);
    SelectObject(dc, old);
}

constexpr UINT kLine = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;

// ============================================================================
// Shapes: GDI+ when available (anti-aliased), plain GDI otherwise
// ============================================================================

Gdiplus::Color gp(COLORREF c) {
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

// Anti-aliased, with pixel i covering [i, i+1] (PixelOffsetModeHalf) - the
// same convention as the SVG mockup, so fills at whole-pixel bounds are exact.
void prepare(Gdiplus::Graphics& g) {
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
}

void addRoundRect(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
    if (r <= 0.0f || w <= 0.0f || h <= 0.0f) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    const float d = std::min(r * 2.0f, std::min(w, h));
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void fillRoundRect(HDC dc, const RECT& rc, float radius, COLORREF color) {
    if (g_gdiplus) {
        Gdiplus::Graphics g(dc);
        prepare(g);
        Gdiplus::GraphicsPath path;
        addRoundRect(path, static_cast<float>(rc.left), static_cast<float>(rc.top),
                     static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top), radius);
        Gdiplus::SolidBrush brush(gp(color));
        g.FillPath(&brush, &path);
        return;
    }
    HBRUSH b = CreateSolidBrush(color);
    HPEN p = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    const int d = static_cast<int>(radius * 2.0f + 0.5f);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, d, d);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

// A rounded rectangle with a border of |borderPx| device pixels, built from
// two fills (outer in the border colour, inner inset by the border width)
// rather than a pen, so the border lands on the outermost pixels.
void borderedRoundRect(HDC dc, const RECT& rc, float radius, int borderPx, COLORREF border, COLORREF fill) {
    fillRoundRect(dc, rc, radius, border);
    RECT inner = rc;
    InflateRect(&inner, -borderPx, -borderPx);
    if (inner.right > inner.left && inner.bottom > inner.top)
        fillRoundRect(dc, inner, std::max(0.0f, radius - static_cast<float>(borderPx)), fill);
}

void fillEllipse(HDC dc, float cx, float cy, float r, COLORREF color) {
    if (g_gdiplus) {
        Gdiplus::Graphics g(dc);
        prepare(g);
        Gdiplus::SolidBrush brush(gp(color));
        g.FillEllipse(&brush, cx - r, cy - r, r * 2.0f, r * 2.0f);
        return;
    }
    HBRUSH b = CreateSolidBrush(color);
    HPEN p = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    Ellipse(dc, static_cast<int>(cx - r), static_cast<int>(cy - r), static_cast<int>(cx + r + 1),
            static_cast<int>(cy + r + 1));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

// Ring whose outer edge fits |rc|: outer disc in |ring|, inner disc in |inside|.
void ringEllipse(HDC dc, const RECT& rc, float width, COLORREF ring, COLORREF inside) {
    const float cx = (rc.left + rc.right) * 0.5f, cy = (rc.top + rc.bottom) * 0.5f;
    const float r = (rc.right - rc.left) * 0.5f;
    fillEllipse(dc, cx, cy, r, ring);
    fillEllipse(dc, cx, cy, std::max(0.0f, r - width), inside);
}

struct PtF { float x, y; };

// Points are in a 0..|unit| design box mapped onto |x|, |y| with |size| px.
void fillPolygon(HDC dc, float x, float y, float size, float unit, std::initializer_list<PtF> pts,
                 COLORREF color) {
    const float k = size / unit;
    if (g_gdiplus) {
        std::vector<Gdiplus::PointF> p;
        for (const PtF& q : pts) p.emplace_back(x + q.x * k, y + q.y * k);
        Gdiplus::Graphics g(dc);
        prepare(g);
        Gdiplus::SolidBrush brush(gp(color));
        g.FillPolygon(&brush, p.data(), static_cast<INT>(p.size()));
        return;
    }
    std::vector<POINT> p;
    for (const PtF& q : pts)
        p.push_back(POINT{static_cast<LONG>(x + q.x * k + 0.5f), static_cast<LONG>(y + q.y * k + 0.5f)});
    HBRUSH b = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pen);
    Polygon(dc, p.data(), static_cast<int>(p.size()));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(pen);
}

void strokePolyline(HDC dc, float x, float y, float size, float unit, std::initializer_list<PtF> pts,
                    float strokeUnits, COLORREF color) {
    const float k = size / unit;
    if (g_gdiplus) {
        std::vector<Gdiplus::PointF> p;
        for (const PtF& q : pts) p.emplace_back(x + q.x * k, y + q.y * k);
        Gdiplus::Graphics g(dc);
        prepare(g);
        Gdiplus::Pen pen(gp(color), strokeUnits * k);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawLines(&pen, p.data(), static_cast<INT>(p.size()));
        return;
    }
    std::vector<POINT> p;
    for (const PtF& q : pts)
        p.push_back(POINT{static_cast<LONG>(x + q.x * k + 0.5f), static_cast<LONG>(y + q.y * k + 0.5f)});
    HPEN pen = CreatePen(PS_SOLID, std::max(1, static_cast<int>(strokeUnits * k + 0.5f)), color);
    HGDIOBJ op = SelectObject(dc, pen);
    Polyline(dc, p.data(), static_cast<int>(p.size()));
    SelectObject(dc, op);
    DeleteObject(pen);
}

void drawDocIcon(HDC dc, const RECT& r) {
    const float x = static_cast<float>(r.left), y = static_cast<float>(r.top);
    const float s = static_cast<float>(r.right - r.left);
    fillPolygon(dc, x, y, s, 32.0f, {{8.0f, 3.5f}, {19.0f, 3.5f}, {26.0f, 10.5f}, {26.0f, 28.5f}, {8.0f, 28.5f}},
                col::docPage);
    fillPolygon(dc, x, y, s, 32.0f, {{19.0f, 3.5f}, {19.0f, 10.5f}, {26.0f, 10.5f}}, col::docFold);
    strokePolyline(dc, x, y, s, 32.0f, {{11.5f, 16.0f}, {22.5f, 16.0f}}, 1.4f, col::docLines);
    strokePolyline(dc, x, y, s, 32.0f, {{11.5f, 19.5f}, {22.5f, 19.5f}}, 1.4f, col::docLines);
    strokePolyline(dc, x, y, s, 32.0f, {{11.5f, 23.0f}, {18.5f, 23.0f}}, 1.4f, col::docLines);
}

// Double-buffered drawing into an item rectangle.
class BufferedPaint {
public:
    BufferedPaint(HDC target, const RECT& rc)
        : target_(target), rc_(rc), w_(rc.right - rc.left), h_(rc.bottom - rc.top) {
        dc_ = CreateCompatibleDC(target);
        bmp_ = CreateCompatibleBitmap(target, std::max(1, w_), std::max(1, h_));
        old_ = SelectObject(dc_, bmp_);
    }
    ~BufferedPaint() {
        BitBlt(target_, rc_.left, rc_.top, w_, h_, dc_, 0, 0, SRCCOPY);
        SelectObject(dc_, old_);
        DeleteObject(bmp_);
        DeleteDC(dc_);
    }
    BufferedPaint(const BufferedPaint&) = delete;
    BufferedPaint& operator=(const BufferedPaint&) = delete;

    HDC  dc() const { return dc_; }
    RECT local() const { return RECT{0, 0, w_, h_}; }

private:
    HDC target_;
    RECT rc_;
    int w_, h_;
    HDC dc_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HGDIOBJ old_ = nullptr;
};

// ============================================================================
// Hover tracking for owner-drawn buttons
// ============================================================================

const wchar_t kPropOldProc[] = L"WdxImportOldProc";
const wchar_t kPropHover[]   = L"WdxImportHover";

bool isHover(HWND h) {
    return GetPropW(h, kPropHover) != nullptr;
}

LRESULT CALLBACK hoverButtonProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    const WNDPROC oldProc = reinterpret_cast<WNDPROC>(GetPropW(h, kPropOldProc));
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!isHover(h)) {
            SetPropW(h, kPropHover, reinterpret_cast<HANDLE>(1));
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            TrackMouseEvent(&tme);
            InvalidateRect(h, nullptr, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        RemovePropW(h, kPropHover);
        InvalidateRect(h, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        RemovePropW(h, kPropHover);
        RemovePropW(h, kPropOldProc);
        if (oldProc) {
            SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oldProc));
            return CallWindowProcW(oldProc, h, msg, wParam, lParam);
        }
        return DefWindowProcW(h, msg, wParam, lParam);
    default:
        break;
    }
    return oldProc ? CallWindowProcW(oldProc, h, msg, wParam, lParam)
                   : DefWindowProcW(h, msg, wParam, lParam);
}

void installHover(HWND hDlg, int id) {
    HWND h = GetDlgItem(hDlg, id);
    if (!h || GetPropW(h, kPropOldProc)) return;
    SetPropW(h, kPropOldProc, reinterpret_cast<HANDLE>(GetWindowLongPtrW(h, GWLP_WNDPROC)));
    SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hoverButtonProc));
}

// ============================================================================
// What the file contains
// ============================================================================

int countFor(const ImportFileInfo& f, int id) {
    switch (id) {
    case IDC_CHK_IMPORT_MATERIALS:  return f.materials;
    case IDC_CHK_IMPORT_TEXTURES:   return f.textures;
    case IDC_CHK_BONES:             return f.bones;
    case IDC_CHK_HELPERS:           return f.helpers;
    case IDC_CHK_LIGHTS:            return f.lights;
    case IDC_CHK_ATTACHMENTS:       return f.attachments;
    case IDC_CHK_PE1:               return f.particleEmitters1;
    case IDC_CHK_PE2:               return f.particleEmitters2;
    case IDC_CHK_EVENT_OBJECTS:     return f.eventObjects;
    case IDC_CHK_RIBBON_EMITTERS:   return f.ribbonEmitters;
    case IDC_CHK_COLLISION_SHAPES:  return f.collisionShapes;
    case IDC_CHK_CAMERAS:           return f.cameras;
    case IDC_CHK_CORN_EMITTERS:     return f.cornEmitters;
    case IDC_CHK_FACEFX:            return f.faceEffects;
    case IDC_CHK_IMPORT_ANIMATIONS: return f.sequences;
    default:                        return -1;
    }
}

bool isObjectRow(int id) {
    return std::find(std::begin(kObjectRows), std::end(kObjectRows), id) != std::end(kObjectRows);
}

bool isAnimRow(int id) {
    return std::find(std::begin(kAnimRows), std::end(kAnimRows), id) != std::end(kAnimRows);
}

// Same visibility rule as before the redesign: Corn Emitters for Reforged or
// whenever the file has CORN chunks (legacy header + Reforged chunks happens),
// FaceFX for Reforged only.
bool rowShown(const ImportFileInfo& f, int id) {
    if (id == IDC_CHK_CORN_EMITTERS) return f.isReforged || f.cornEmitters > 0;
    if (id == IDC_CHK_FACEFX)        return f.isReforged;
    return true;
}

bool anyObjects(const ImportFileInfo& f) {
    for (int id : kObjectRows)
        if (rowShown(f, id) && countFor(f, id) > 0) return true;
    return false;
}

// Does the file contain anything this checkbox would import?
bool present(const ImportFileInfo& f, int id) {
    if (id == IDC_CHK_SKINNED)           return f.bones > 0;
    if (id == IDC_CHK_IMPORT_OBJECTS)    return anyObjects(f);
    if (id == IDC_CHK_IMPORT_ANIMATIONS || isAnimRow(id)) return f.sequences > 0;
    if (isObjectRow(id))                 return rowShown(f, id) && countFor(f, id) > 0;
    if (id == IDC_CHK_IMPORT_MATERIALS)  return f.materials > 0;
    if (id == IDC_CHK_IMPORT_TEXTURES)   return f.textures > 0;
    return true;  // options
}

// ============================================================================
// Fast Settings — the one table of preset rules
// ============================================================================
//
// Same rules as the former onFastSettingsChanged (and consistent with
// applyFastPreset, which the plug-in runs after the dialog). Index = position
// in the drop-down = ir::CoreImportOptions::Preset. Returns false for Custom,
// which decides nothing.

bool presetValue(int preset, int id, bool& value) {
    const bool objectRow = isObjectRow(id) || id == IDC_CHK_IMPORT_OBJECTS;
    const bool animRow = isAnimRow(id) || id == IDC_CHK_IMPORT_ANIMATIONS;
    const bool material = id == IDC_CHK_IMPORT_MATERIALS || id == IDC_CHK_IMPORT_TEXTURES;
    const bool bonesHelpers = id == IDC_CHK_IMPORT_OBJECTS || id == IDC_CHK_BONES || id == IDC_CHK_HELPERS;

    switch (preset) {
    case 1:  // Static No Materials
    case 2:  // Static Materials
        if (material) { value = (preset == 2); return true; }
        value = false;
        return id == IDC_CHK_SKINNED || objectRow || animRow;
    case 3:  // Animated No Skinning
    case 4:  // Animated No Objects
        if (material || animRow) { value = true; return true; }
        if (id == IDC_CHK_SKINNED) { value = (preset == 4); return true; }
        if (objectRow) { value = bonesHelpers; return true; }
        return false;
    case 5:  // All
        value = true;
        return material || animRow || objectRow || id == IDC_CHK_SKINNED;
    default: // Custom
        return false;
    }
}

bool matchesPreset(HWND hDlg, const DialogState* ds, int preset) {
    for (int id : kFastIds) {
        bool v = false;
        if (!presetValue(preset, id, v)) continue;
        if (getCheck(hDlg, id) != (v && present(*ds->file, id))) return false;
    }
    return true;
}

void applyPreset(HWND hDlg, DialogState* ds, int preset) {
    for (int id : kFastIds) {
        bool v = false;
        if (presetValue(preset, id, v))
            setCheck(hDlg, id, v && present(*ds->file, id));
    }
}

// Enabled state: a checkbox is usable when the file has something for it and,
// for sub-rows, when its master checkbox is on. Counts follow their checkbox.
void applyEnabledState(HWND hDlg, const DialogState* ds) {
    const ImportFileInfo& f = *ds->file;
    const bool objectsOn = getCheck(hDlg, IDC_CHK_IMPORT_OBJECTS);
    const bool animsOn = getCheck(hDlg, IDC_CHK_IMPORT_ANIMATIONS);
    for (int id : kFastIds) {
        bool on = present(f, id);
        if (isObjectRow(id)) on = on && objectsOn;
        if (isAnimRow(id))   on = on && animsOn;
        enableCtrl(hDlg, id, on);
    }
    for (int id : kCountedIds)
        enableCtrl(hDlg, id + IDC_CNT_OFFSET, IsWindowEnabled(GetDlgItem(hDlg, id)) != FALSE);
    for (int id : kOptionRows)
        enableCtrl(hDlg, id, true);
}

// After the user changed a checkbox: leave the preset if it no longer matches.
void refreshPresetAfterEdit(HWND hDlg, DialogState* ds) {
    if (ds->preset != 0 && !matchesPreset(hDlg, ds, ds->preset)) {
        ds->preset = 0;
        SendDlgItemMessageW(hDlg, IDC_CMB_FAST_SETTINGS, CB_SETCURSEL, 0, 0);
    }
}

// ============================================================================
// INI persistence (same file and keys as before the redesign)
// ============================================================================

std::wstring getINIPath(Interface* gi) {
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
    return std::wstring(dir.data()) + L"\\WhiteoutDexImporter.ini";
}

// Disambiguate Win32 API from MaxSDK::Util wrappers (same pattern as mdlx_import_options.cpp)
static auto Win32_WritePrivateProfileStringW =
    static_cast<BOOL(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR)>(
        &::WritePrivateProfileStringW);

// Only categories the file contains are written: a value the user could not
// see or change stays as it was saved.
void saveDialogSettingsToINI(HWND hDlg, const DialogState* ds) {
    const std::wstring iniPath = getINIPath(GetCOREInterface());
    const wchar_t* sec = L"Settings";
    const ImportFileInfo& f = *ds->file;

    auto writeB = [&](const wchar_t* key, bool val) {
        Win32_WritePrivateProfileStringW(sec, key, val ? L"true" : L"false", iniPath.c_str());
    };

    for (const CoreRow& r : kCoreRows)
        if (present(f, r.id))
            writeB(r.iniKey, getCheck(hDlg, r.id));

    if (rowShown(f, IDC_CHK_CORN_EMITTERS) && present(f, IDC_CHK_CORN_EMITTERS))
        writeB(L"ImportCornEmitters", getCheck(hDlg, IDC_CHK_CORN_EMITTERS));
    if (f.isReforged && present(f, IDC_CHK_FACEFX))
        writeB(L"ImportFaceFX", getCheck(hDlg, IDC_CHK_FACEFX));

    // The loader reads "Merge"; the old dialog wrote true/false here, which the
    // loader never recognised, so Merge was never restored.
    Win32_WritePrivateProfileStringW(sec, L"ImportMode", ds->merge ? L"Merge" : L"NewScene", iniPath.c_str());

    // 1-based, as before (1 = Custom ... 6 = All)
    wchar_t buf[16];
    _itow_s(ds->preset + 1, buf, 10);
    Win32_WritePrivateProfileStringW(sec, L"FastSettings", buf, iniPath.c_str());
}

// ── Options struct <-> dialog ──────────────────────────────

void optionsToDialog(HWND hDlg, DialogState* ds) {
    const MdlxImportOptions& opts = *ds->opts;
    for (const CoreRow& r : kCoreRows)
        setCheck(hDlg, r.id, opts.core.*(r.field));
    setCheck(hDlg, IDC_CHK_CORN_EMITTERS, opts.importCornEmitters);
    setCheck(hDlg, IDC_CHK_FACEFX, opts.importFaceFX);

    ds->merge = (opts.core.mode == ir::CoreImportOptions::ImportMode::Merge);
    ds->preset = std::min(5, std::max(0, static_cast<int>(opts.core.preset)));
}

// Categories the file does not contain keep the value they came in with.
void dialogToOptions(HWND hDlg, const DialogState* ds, MdlxImportOptions& opts) {
    const ImportFileInfo& f = *ds->file;
    for (const CoreRow& r : kCoreRows)
        if (present(f, r.id))
            opts.core.*(r.field) = getCheck(hDlg, r.id);

    if (rowShown(f, IDC_CHK_CORN_EMITTERS) && present(f, IDC_CHK_CORN_EMITTERS))
        opts.importCornEmitters = getCheck(hDlg, IDC_CHK_CORN_EMITTERS);
    if (f.isReforged && present(f, IDC_CHK_FACEFX))
        opts.importFaceFX = getCheck(hDlg, IDC_CHK_FACEFX);

    opts.core.mode = ds->merge ? ir::CoreImportOptions::ImportMode::Merge
                               : ir::CoreImportOptions::ImportMode::NewScene;
    opts.core.preset = static_cast<ir::CoreImportOptions::Preset>(ds->preset);
}

// ============================================================================
// Localization
// ============================================================================

constexpr wdx::l10n::DialogString kImportStrings[] = {
    {IDC_LBL_FAST_SETTINGS, "imp_fast_settings_lbl"},
    {IDC_LBL_MODE, "imp_mode_lbl"},
    {IDC_BTN_TAB_OBJECTS, "imp_objects_grp"},
    {IDC_BTN_TAB_ANIMATIONS, "imp_animations_grp"},
    {IDC_BTN_TAB_MODEL, "imp_model_tab"},

    {IDC_CHK_IMPORT_OBJECTS, "imp_import_objects_chk"},
    {IDC_CHK_BONES, "imp_bones_chk"},
    {IDC_CHK_HELPERS, "imp_helpers_chk"},
    {IDC_CHK_LIGHTS, "imp_lights_chk"},
    {IDC_CHK_ATTACHMENTS, "imp_attachments_chk"},
    {IDC_CHK_PE1, "imp_particle_emitters_1_chk"},
    {IDC_CHK_PE2, "imp_particle_emitters_2_chk"},
    {IDC_CHK_EVENT_OBJECTS, "imp_event_objects_chk"},
    {IDC_CHK_RIBBON_EMITTERS, "imp_ribbon_emitters_chk"},
    {IDC_CHK_COLLISION_SHAPES, "imp_collision_shapes_chk"},
    {IDC_CHK_CAMERAS, "imp_cameras_chk"},
    {IDC_CHK_CORN_EMITTERS, "imp_corn_emitters_chk"},
    {IDC_CHK_FACEFX, "imp_facefx_chk"},
    {IDC_LBL_NO_OBJECTS, "imp_no_objects_lbl"},

    {IDC_CHK_IMPORT_ANIMATIONS, "imp_import_animations_chk"},
    {IDC_CHK_TRANSLATION, "imp_translation_chk"},
    {IDC_CHK_ROTATIONS, "imp_rotations_chk"},
    {IDC_CHK_SCALE, "imp_scale_chk"},
    {IDC_CHK_PARAMETER, "imp_parameter_chk"},
    {IDC_CHK_UNWRAP_ANIMS, "imp_unwrap_animations_chk"},
    {IDC_CHK_TEXTURE_ANIMS, "imp_texture_animations_chk"},
    {IDC_CHK_VISIBILITY, "imp_visibility_chk"},
    {IDC_CHK_COLOR, "imp_color_chk"},
    {IDC_LBL_NO_ANIMATIONS, "imp_no_animations_lbl"},

    {IDC_CHK_SKINNED, "imp_skinned_chk"},
    {IDC_CHK_IMPORT_MATERIALS, "imp_import_materials_chk"},
    {IDC_CHK_IMPORT_TEXTURES, "imp_import_textures_chk"},
    {IDC_LBL_OPTIONS, "imp_options_lbl"},
    {IDC_CHK_POINT_HELPERS, "imp_import_helpers_as_point_helpers_chk"},
    {IDC_CHK_OPT_GEOMETRY, "imp_optimize_geometry_chk"},
    {IDC_CHK_OPT_BONES, "imp_optimize_bones_and_helpers_chk"},

    {IDOK, "imp_import_btn"},
    {IDCANCEL, "common_cancel_btn"},
};

constexpr int kModeIds[] = {IDC_BTN_MODE_NEW_SCENE, IDC_BTN_MODE_MERGE};

// A translation, or |fallback| when the catalog has none (WdxL.t answers a
// missing key with the key itself).
std::wstring usable(const std::wstring& value, const char* key, const wchar_t* fallback) {
    if (value.empty() || value == wdx::l10n::detail::Utf8ToWide(key)) return fallback;
    return value;
}

struct DrawnText {
    std::wstring subNew, subMerge, contains, badgeClassic, badgeReforged;
    std::wstring texMpq, texCasc, texNone, title, titleReforged, presets;
};

DrawnText loadDrawnText() {
    static const char* keys[] = {
        "imp_mode_new_scene_sub", "imp_mode_merge_sub", "imp_contains_fmt",
        "imp_badge_classic", "imp_badge_reforged",
        "imp_textures_mpq_lbl", "imp_textures_casc_lbl", "imp_textures_none_lbl",
        "imp_whiteoutdex_importer_title", "imp_whiteoutdex_importer_reforged_title",
        "imp_fast_settings_labels",
    };
    const std::vector<std::wstring> v = wdx::l10n::TranslateMany(std::vector<const char*>(std::begin(keys), std::end(keys)));
    auto at = [&](size_t i) { return i < v.size() ? v[i] : std::wstring(); };

    DrawnText t;
    t.subNew        = usable(at(0), keys[0], L"Replaces the scene");
    t.subMerge      = usable(at(1), keys[1], L"Adds to the scene");
    t.contains      = usable(at(2), keys[2], L"contains: %1 bones \u00B7 %2 geosets \u00B7 %3 animations");
    t.badgeClassic  = usable(at(3), keys[3], L"Classic");
    t.badgeReforged = usable(at(4), keys[4], L"Reforged");
    t.texMpq        = usable(at(5), keys[5], L"Textures: MPQ archives");
    t.texCasc       = usable(at(6), keys[6], L"Textures: CASC archives");
    t.texNone       = usable(at(7), keys[7], L"Textures: no archive folder set");
    t.title         = usable(at(8), keys[8], L"WhiteoutDex Importer");
    t.titleReforged = usable(at(9), keys[9], L"WhiteoutDex Importer (Reforged)");
    t.presets       = usable(at(10), keys[10],
                             L"Custom|Static No Materials|Static Materials|Animated No Skinning|Animated No objects|All");

    // A translated format string must keep all three placeholders.
    if (t.contains.find(L"%1") == std::wstring::npos || t.contains.find(L"%2") == std::wstring::npos ||
        t.contains.find(L"%3") == std::wstring::npos)
        t.contains = L"contains: %1 bones \u00B7 %2 geosets \u00B7 %3 animations";
    return t;
}

void initPresetCombo(HWND hDlg, const std::wstring& joined) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (;;) {
        const size_t bar = joined.find(L'|', start);
        parts.push_back(joined.substr(start, bar == std::wstring::npos ? bar : bar - start));
        if (bar == std::wstring::npos) break;
        start = bar + 1;
    }
    static const wchar_t* kEnglish[6] = {L"Custom", L"Static No Materials", L"Static Materials",
                                         L"Animated No Skinning", L"Animated No objects", L"All"};
    HWND h = GetDlgItem(hDlg, IDC_CMB_FAST_SETTINGS);
    SendMessageW(h, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < 6; ++i) {
        const std::wstring& s = (parts.size() == 6 && !parts[static_cast<size_t>(i)].empty())
                                    ? parts[static_cast<size_t>(i)] : std::wstring(kEnglish[i]);
        SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str()));
    }
}

void buildHeaderAndFooter(DialogState* ds, const DrawnText& t) {
    const ImportFileInfo& f = *ds->file;
    const size_t sl = f.path.find_last_of(L"\\/");
    ds->headerName = (sl == std::wstring::npos) ? f.path : f.path.substr(sl + 1);
    ds->headerBadge = (f.isReforged ? t.badgeReforged : t.badgeClassic) + L" \u00B7 v" + std::to_wstring(f.version);

    std::wstring line = t.contains;
    wdx::l10n::Substitute(line, L"%1", std::to_wstring(f.bones));
    wdx::l10n::Substitute(line, L"%2", std::to_wstring(f.geosets));
    wdx::l10n::Substitute(line, L"%3", std::to_wstring(f.sequences));
    ds->headerLine = line;

    // The resolver searches whichever archive folder is configured; the loader
    // already copies one into the other when only one is set.
    const MdlxImportOptions& o = *ds->opts;
    const bool casc = !o.cascDirectory.empty();
    const bool mpq = !o.mpqArchives.empty() || !o.mpqDirectory.empty();
    if (f.isReforged)
        ds->footerText = casc ? t.texCasc : (mpq ? t.texMpq : t.texNone);
    else
        ds->footerText = mpq ? t.texMpq : (casc ? t.texCasc : t.texNone);
    ds->footerWarn = !casc && !mpq;
}

// ============================================================================
// Layout and visibility
// ============================================================================

// Visible object rows in reading order.
std::vector<int> shownObjectRows(const ImportFileInfo& f) {
    std::vector<int> rows;
    for (int id : kObjectRows)
        if (rowShown(f, id)) rows.push_back(id);
    return rows;
}

int gridRows(size_t n) { return static_cast<int>((n + 1) / 2); }

// Height (96-DPI px) of each tab page for this file; the page uses the tallest.
int computePageHeight(const ImportFileInfo& f) {
    const int master = lay::rowH + lay::rowGap;  // master row + gap before the grid
    const int note = master + lay::noteH;
    const int objects = anyObjects(f) ? master + gridRows(shownObjectRows(f).size()) * lay::rowPitch - lay::rowGap
                                      : note;
    const int anims = f.sequences > 0 ? master + 4 * lay::rowPitch - lay::rowGap : note;
    const int model = lay::optionsFirstRowY + 2 * lay::rowPitch + lay::rowH - lay::pageY;
    return std::max(objects, std::max(anims, model));
}

// A checkbox row with an optional count on its right.
void placeRow(HWND hDlg, const DialogState* ds, int id, int x, int y, int w) {
    const bool counted = countFor(*ds->file, id) >= 0;
    const int chkW = counted ? w - lay::countW - lay::countGap : w;
    place(hDlg, ds, id, x, y, chkW, lay::rowH);
    if (counted)
        place(hDlg, ds, id + IDC_CNT_OFFSET, x + w - lay::countW, y + 1, lay::countW, lay::rowH - 2);
}

void layoutImportDialog(HWND hDlg, DialogState* ds) {
    const ImportFileInfo& f = *ds->file;
    ds->pageH = computePageHeight(f);
    ds->footerY = lay::pageY + ds->pageH + lay::footerPad;
    const int clientH = ds->footerY + lay::footerH;

    RECT rc{0, 0, ds->S(lay::clientW), ds->S(clientH)};
    AdjustWindowRectEx(&rc, static_cast<DWORD>(GetWindowLongPtrW(hDlg, GWL_STYLE)), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(hDlg, GWL_EXSTYLE)));
    SetWindowPos(hDlg, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    place(hDlg, ds, IDC_LBL_FAST_SETTINGS, lay::lblFast.x, lay::lblFast.y, lay::lblFast.w, lay::lblFast.h);
    placeCombo(hDlg, ds, IDC_CMB_FAST_SETTINGS, ds->S(lay::cmbFast.x), ds->S(lay::cmbFast.y),
               ds->S(lay::cmbFast.x + lay::cmbFast.w) - ds->S(lay::cmbFast.x), ds->S(lay::cmbFast.h));
    place(hDlg, ds, IDC_LBL_MODE, lay::lblMode.x, lay::lblMode.y, lay::lblMode.w, lay::lblMode.h);
    place(hDlg, ds, IDC_BTN_MODE_NEW_SCENE, lay::cardNew.x, lay::cardNew.y, lay::cardNew.w, lay::cardNew.h);
    place(hDlg, ds, IDC_BTN_MODE_MERGE, lay::cardMerge.x, lay::cardMerge.y, lay::cardMerge.w, lay::cardMerge.h);

    // Tabs are as wide as their text, 22 px apart.
    int x = ds->S(lay::tabX);
    for (int i = 0; i < 3; ++i) {
        const int w = textWidth(hDlg, ds->font, windowText(GetDlgItem(hDlg, kTabIds[i]))) + ds->S(2);
        placeDevice(hDlg, kTabIds[i], x, ds->S(lay::tabY), w, ds->S(lay::tabH));
        ds->tabRect[i] = RECT{x, ds->S(lay::tabY), x + w, ds->S(lay::tabY + lay::tabH)};
        x += w + ds->S(lay::tabGap);
    }

    // Objects tab
    const int gridY = lay::pageY + lay::rowPitch;
    place(hDlg, ds, IDC_CHK_IMPORT_OBJECTS, lay::pageX, lay::pageY, lay::pageW, lay::rowH);
    const std::vector<int> objs = shownObjectRows(f);
    for (size_t i = 0; i < objs.size(); ++i) {
        const int colX = (i % 2 == 0) ? lay::subX : lay::subX + lay::colW0 + lay::colGap;
        const int colW = (i % 2 == 0) ? lay::colW0 : lay::colW1;
        placeRow(hDlg, ds, objs[i], colX, gridY + static_cast<int>(i / 2) * lay::rowPitch, colW);
    }
    place(hDlg, ds, IDC_LBL_NO_OBJECTS, lay::subX, gridY, lay::pageX + lay::pageW - lay::subX, lay::noteH);

    // Animations tab
    placeRow(hDlg, ds, IDC_CHK_IMPORT_ANIMATIONS, lay::pageX, lay::pageY, lay::pageW);
    for (size_t i = 0; i < std::size(kAnimRows); ++i) {
        const int colX = (i % 2 == 0) ? lay::subX : lay::subX + lay::colW0 + lay::colGap;
        const int colW = (i % 2 == 0) ? lay::colW0 : lay::colW1;
        place(hDlg, ds, kAnimRows[i], colX, gridY + static_cast<int>(i / 2) * lay::rowPitch, colW, lay::rowH);
    }
    place(hDlg, ds, IDC_LBL_NO_ANIMATIONS, lay::subX, gridY, lay::pageX + lay::pageW - lay::subX, lay::noteH);

    // Model tab
    for (size_t i = 0; i < std::size(kModelRows); ++i)
        placeRow(hDlg, ds, kModelRows[i], lay::pageX, lay::pageY + static_cast<int>(i) * lay::rowPitch, lay::pageW);
    place(hDlg, ds, IDC_LBL_OPTIONS, lay::pageX, lay::optionsLabelY, lay::pageW, lay::noteH);
    for (size_t i = 0; i < std::size(kOptionRows); ++i)
        place(hDlg, ds, kOptionRows[i], lay::pageX, lay::optionsFirstRowY + static_cast<int>(i) * lay::rowPitch,
              lay::pageW, lay::rowH);

    // Footer buttons from the right edge
    const int btnPad = ds->S(lay::btnPad) * 2;
    const int cancelW = std::max(ds->S(lay::btnMinW),
                                 textWidth(hDlg, ds->font, windowText(GetDlgItem(hDlg, IDCANCEL))) + btnPad);
    const int importW = std::max(ds->S(lay::btnMinW),
                                 textWidth(hDlg, ds->font, windowText(GetDlgItem(hDlg, IDOK))) + btnPad);
    const int btnY = ds->S(ds->footerY + 12);
    const int cancelX = ds->S(lay::right) - cancelW;
    const int importX = cancelX - ds->S(lay::btnGap) - importW;
    placeDevice(hDlg, IDCANCEL, cancelX, btnY, cancelW, ds->S(lay::btnH));
    placeDevice(hDlg, IDOK, importX, btnY, importW, ds->S(lay::btnH));
    ds->importX = importX;
}

// Which controls belong to which tab, and which rows this file shows.
void applyPageVisibility(HWND hDlg, const DialogState* ds) {
    const ImportFileInfo& f = *ds->file;
    const bool objTab = ds->tab == 0, animTab = ds->tab == 1, modelTab = ds->tab == 2;
    const bool haveObjects = anyObjects(f);
    const bool haveAnims = f.sequences > 0;

    showCtrl(hDlg, IDC_CHK_IMPORT_OBJECTS, objTab);
    for (int id : kObjectRows) {
        const bool show = objTab && haveObjects && rowShown(f, id);
        showCtrl(hDlg, id, show);
        showCtrl(hDlg, id + IDC_CNT_OFFSET, show);
    }
    showCtrl(hDlg, IDC_LBL_NO_OBJECTS, objTab && !haveObjects);

    showCtrl(hDlg, IDC_CHK_IMPORT_ANIMATIONS, animTab);
    showCtrl(hDlg, IDC_CNT_IMPORT_ANIMATIONS, animTab);
    for (int id : kAnimRows)
        showCtrl(hDlg, id, animTab && haveAnims);
    showCtrl(hDlg, IDC_LBL_NO_ANIMATIONS, animTab && !haveAnims);

    for (int id : kModelRows)
        showCtrl(hDlg, id, modelTab);
    showCtrl(hDlg, IDC_CNT_IMPORT_MATERIALS, modelTab);
    showCtrl(hDlg, IDC_CNT_IMPORT_TEXTURES, modelTab);
    showCtrl(hDlg, IDC_LBL_OPTIONS, modelTab);
    for (int id : kOptionRows)
        showCtrl(hDlg, id, modelTab);
}

void setTabStop(HWND hDlg, int id, bool on) {
    HWND h = GetDlgItem(hDlg, id);
    if (!h) return;
    const LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    SetWindowLongPtrW(h, GWL_STYLE, on ? (style | WS_TABSTOP) : (style & ~static_cast<LONG_PTR>(WS_TABSTOP)));
}

// Only the selected card and the active tab are tab stops; arrow keys move
// within each group (radio-button behaviour).
void updateTabStops(HWND hDlg, const DialogState* ds) {
    setTabStop(hDlg, IDC_BTN_MODE_NEW_SCENE, !ds->merge);
    setTabStop(hDlg, IDC_BTN_MODE_MERGE, ds->merge);
    for (int i = 0; i < 3; ++i)
        setTabStop(hDlg, kTabIds[i], ds->tab == i);
}

void selectMode(HWND hDlg, DialogState* ds, bool merge) {
    if (ds->merge == merge) return;
    ds->merge = merge;
    updateTabStops(hDlg, ds);
    InvalidateRect(GetDlgItem(hDlg, IDC_BTN_MODE_NEW_SCENE), nullptr, FALSE);
    InvalidateRect(GetDlgItem(hDlg, IDC_BTN_MODE_MERGE), nullptr, FALSE);
}

void selectTab(HWND hDlg, DialogState* ds, int tab) {
    if (ds->tab == tab) return;
    ds->tab = tab;
    updateTabStops(hDlg, ds);
    for (int id : kTabIds)
        InvalidateRect(GetDlgItem(hDlg, id), nullptr, FALSE);
    RECT r{0, ds->S(lay::underlineY), ds->S(lay::clientW), ds->S(lay::underlineY + lay::underlineH)};
    InvalidateRect(hDlg, &r, FALSE);
    applyPageVisibility(hDlg, ds);
}

// ============================================================================
// Painting
// ============================================================================

void paintProgress(HWND hDlg, HDC dc, const DialogState* ds);

// Everything the dialog paints itself, drawn into |target| (client
// coordinates) - WM_PAINT's DC, or a child control's DC when it asks for the
// dialog background behind it (see WM_ERASEBKGND / WM_PRINTCLIENT).
void drawDialogInto(HWND hDlg, DialogState* ds, HDC target) {
    RECT client{};
    GetClientRect(hDlg, &client);
    {
        BufferedPaint buffer(target, client);
        HDC dc = buffer.dc();

        fillRectColor(dc, client, col::body);

        // Header band: document icon, file name + version badge, contents line
        fillRectColor(dc, RECT{0, 0, client.right, ds->S(lay::headerH)}, col::band);
        fillRectColor(dc, RECT{0, ds->S(lay::headerLineY), client.right,
                               ds->S(lay::headerLineY) + std::max(1, ds->S(1))}, col::bandLine);
        drawDocIcon(dc, ds->R(lay::docIcon));

        const int textRight = ds->S(lay::right);
        const int badgeTextW = textWidth(hDlg, ds->fontSmall, ds->headerBadge);
        const int badgeW = badgeTextW + ds->S(lay::badgePadX) * 2 + 2;
        const int nameMax = std::max(ds->S(40), textRight - ds->S(lay::nameX) - ds->S(lay::badgeGap) - badgeW);
        const int nameW = std::min(nameMax, textWidth(hDlg, ds->fontSemibold, ds->headerName));
        drawTextIn(dc, ds->headerName,
                   RECT{ds->S(lay::nameX), ds->S(lay::nameY), ds->S(lay::nameX) + nameW,
                        ds->S(lay::nameY + lay::textH)},
                   ds->fontSemibold, col::textBright, kLine);

        const bool reforged = ds->file->isReforged;
        const int badgeX = ds->S(lay::nameX) + nameW + ds->S(lay::badgeGap);
        const RECT badge{badgeX, ds->S(lay::nameY), badgeX + badgeW, ds->S(lay::nameY + lay::badgeH)};
        borderedRoundRect(dc, badge, ds->Sf(8.0f), std::max(1, ds->S(1)),
                          reforged ? col::accent : col::badgeClassicBorder, col::band);
        drawTextIn(dc, ds->headerBadge, badge, ds->fontSmall, reforged ? col::badgeReforged : col::badgeClassic,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        drawTextIn(dc, ds->headerLine,
                   RECT{ds->S(lay::nameX), ds->S(lay::lineY), textRight, ds->S(lay::lineY + lay::textH)},
                   ds->font, col::textDim, kLine);

        // Tab strip line and the active tab's underline
        const RECT strip = ds->R(lay::tabStrip);
        fillRectColor(dc, RECT{strip.left, strip.top, strip.right, strip.top + std::max(1, ds->S(1))}, col::tabLine);
        RECT underline = ds->tabRect[ds->tab];
        underline.top = ds->S(lay::underlineY);
        underline.bottom = underline.top + ds->S(lay::underlineH);
        fillRectColor(dc, underline, col::accent);

        // Footer band: the texture archive status, or the progress once the
        // import runs
        const int fy = ds->S(ds->footerY);
        fillRectColor(dc, RECT{0, fy, client.right, client.bottom}, col::band);
        fillRectColor(dc, RECT{0, fy, client.right, fy + std::max(1, ds->S(1))}, col::bandLine);
        if (ds->busy) {
            paintProgress(hDlg, dc, ds);
        } else {
            drawTextIn(dc, ds->footerText,
                       RECT{ds->S(lay::pageX), ds->S(ds->footerY + 12), ds->importX - ds->S(12),
                            ds->S(ds->footerY + 12 + lay::btnH)},
                       ds->font, ds->footerWarn ? col::textWarn : col::textDim, kLine);
        }
    }
}

void paintDialog(HWND hDlg, DialogState* ds) {
    PAINTSTRUCT ps;
    HDC target = BeginPaint(hDlg, &ps);
    drawDialogInto(hDlg, ds, target);
    EndPaint(hDlg, &ps);
}

// ============================================================================
// Progress (after Import)
// ============================================================================

// Footer while the import runs: step text and percentage on one row, the bar
// below, across the width of the content.
void paintProgress(HWND hDlg, HDC dc, const DialogState* ds) {
    const int x0 = ds->S(lay::pageX), x1 = ds->S(lay::right);
    const int textTop = ds->S(ds->footerY + lay::progTextY);
    const int textBottom = ds->S(ds->footerY + lay::progTextY + lay::textH);

    const int pct = static_cast<int>(ds->progress * 100.0f + 0.5f);
    const std::wstring pctText = std::to_wstring(std::min(100, std::max(0, pct))) + L"%";
    const int pctW = textWidth(hDlg, ds->font, pctText);
    drawTextIn(dc, pctText, RECT{x1 - pctW, textTop, x1, textBottom}, ds->font, col::textDim,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    std::wstring text = (ds->step >= 0 && ds->step < static_cast<int>(ds->stepText.size()))
                            ? ds->stepText[static_cast<size_t>(ds->step)] : std::wstring();
    if (ds->itemsTotal > 1)
        text += L"  " + std::to_wstring(std::min(ds->itemsDone + 1, ds->itemsTotal)) + L" / " +
                std::to_wstring(ds->itemsTotal);
    drawTextIn(dc, text, RECT{x0, textTop, x1 - pctW - ds->S(12), textBottom}, ds->font, col::text, kLine);

    const RECT track{x0, ds->S(ds->footerY + lay::progBarY), x1,
                     ds->S(ds->footerY + lay::progBarY + lay::progBarH)};
    const float radius = (track.bottom - track.top) * 0.5f;
    fillRoundRect(dc, track, radius, col::tabLine);
    // At least as wide as high, so the rounded ends never overlap.
    const LONG fillW = static_cast<LONG>((track.right - track.left) * std::min(1.0f, std::max(0.0f, ds->progress)));
    if (fillW > 0)
        fillRoundRect(dc, RECT{track.left, track.top, track.left + std::max(fillW, track.bottom - track.top),
                               track.bottom},
                      radius, col::accent);
}

// Leaves the options: Import and Cancel disappear, every control is disabled,
// and the footer shows the progress from now on.
void beginProgress(HWND hDlg, DialogState* ds) {
    const std::vector<const char*> keys = [] {
        std::vector<const char*> k;
        for (const StepInfo& s : kSteps) k.push_back(s.key);
        return k;
    }();
    const std::vector<std::wstring> v = wdx::l10n::TranslateMany(keys);
    ds->stepText.clear();
    for (size_t i = 0; i < std::size(kSteps); ++i)
        ds->stepText.push_back(usable(i < v.size() ? v[i] : std::wstring(), kSteps[i].key, kSteps[i].english));

    ds->busy = true;
    ShowWindow(GetDlgItem(hDlg, IDOK), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDCANCEL), SW_HIDE);
    for (HWND c = GetWindow(hDlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        EnableWindow(c, FALSE);
    InvalidateRect(hDlg, nullptr, FALSE);
    UpdateWindow(hDlg);
}

// Repaints the footer (at most every 50 ms unless |force|) and keeps the
// window responsive: the import runs on this thread and dispatches nothing,
// and Windows treats a thread that has not called PeekMessage for 5 seconds as
// not responding (IsHungAppWindow). PM_NOREMOVE only looks: Max is disabled,
// so what the user types or clicks meanwhile is aimed at this window, which
// no longer exists when Max reads its queue again.
void refreshProgress(HWND hDlg, DialogState* ds, bool force) {
    const ULONGLONG now = GetTickCount64();
    if (force || now - ds->lastPaint >= 50) {
        ds->lastPaint = now;
        RECT r{0, ds->S(ds->footerY), ds->S(lay::clientW), ds->S(ds->footerY + lay::footerH)};
        InvalidateRect(hDlg, &r, FALSE);
        UpdateWindow(hDlg);
    }
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);
}

bool focusVisible(const DRAWITEMSTRUCT* dis) {
    return (dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT);
}

void drawModeCard(const DRAWITEMSTRUCT* dis, const DialogState* ds, bool mergeCard) {
    BufferedPaint buffer(dis->hDC, dis->rcItem);
    HDC dc = buffer.dc();
    const RECT rc = buffer.local();
    const bool selected = ds->merge == mergeCard;
    const bool hover = isHover(dis->hwndItem);

    fillRectColor(dc, rc, col::body);
    const COLORREF fill = selected ? col::cardSelBg : col::cardBg;
    const int borderPx = std::max(1, ds->S(1)) * (selected ? 2 : 1);
    borderedRoundRect(dc, rc, ds->Sf(4.0f), borderPx,
                      selected ? col::accent : (hover ? col::cardHover : col::cardBorder), fill);

    const RECT radio{ds->S(10), ds->S(7), ds->S(23), ds->S(20)};
    ringEllipse(dc, radio, ds->Sf(1.5f), selected ? col::accent : col::radioRing, fill);
    if (selected)
        fillEllipse(dc, (radio.left + radio.right) * 0.5f, (radio.top + radio.bottom) * 0.5f, ds->Sf(2.5f),
                    col::accent);

    drawTextIn(dc, windowText(dis->hwndItem), RECT{ds->S(30), ds->S(5), rc.right - ds->S(9), ds->S(21)},
               ds->fontSemibold, col::cardTitle, kLine);
    drawTextIn(dc, mergeCard ? ds->subMerge : ds->subNewScene,
               RECT{ds->S(30), ds->S(21), rc.right - ds->S(9), ds->S(37)}, ds->font, col::cardSub, kLine);

    if (focusVisible(dis))
        frameRectColor(dc, inset(rc, ds->S(3)), col::focus);
}

void drawTab(const DRAWITEMSTRUCT* dis, const DialogState* ds, int index) {
    BufferedPaint buffer(dis->hDC, dis->rcItem);
    HDC dc = buffer.dc();
    const RECT rc = buffer.local();
    const bool selected = ds->tab == index;

    fillRectColor(dc, rc, col::body);
    drawTextIn(dc, windowText(dis->hwndItem), rc, ds->font,
               selected ? col::tabSelected : (isHover(dis->hwndItem) ? col::tabHover : col::tabText), kLine);
    if (focusVisible(dis))
        frameRectColor(dc, rc, col::accent);
}

void drawImportButton(const DRAWITEMSTRUCT* dis, const DialogState* ds) {
    BufferedPaint buffer(dis->hDC, dis->rcItem);
    HDC dc = buffer.dc();
    const RECT rc = buffer.local();
    const COLORREF fill = (dis->itemState & ODS_SELECTED) ? col::primaryDown
                        : (isHover(dis->hwndItem) ? col::primaryHover : col::primary);

    fillRectColor(dc, rc, col::band);
    fillRoundRect(dc, rc, ds->Sf(3.0f), fill);
    drawTextIn(dc, windowText(dis->hwndItem), rc, ds->font, col::primaryText,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (focusVisible(dis))
        frameRectColor(dc, inset(rc, ds->S(3)), col::focus);
}

// ============================================================================
// Resources
// ============================================================================

void applyDarkCaption(HWND hDlg) {
    // Same attributes as the renderer window. The caption colour only exists
    // on Windows 11; older systems ignore it and keep their normal title bar.
    const BOOL useDark = TRUE;
    const COLORREF chrome = col::band;
    DwmSetWindowAttribute(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    DwmSetWindowAttribute(hDlg, DWMWA_CAPTION_COLOR, &chrome, sizeof(chrome));
    DwmSetWindowAttribute(hDlg, DWMWA_BORDER_COLOR, &chrome, sizeof(chrome));
}

HFONT makeFont(int dpi, int points, int weight) {
    return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void createResources(DialogState* ds) {
    ds->font = makeFont(ds->dpi, 9, FW_NORMAL);             // Segoe UI 9 pt (12 px)
    ds->fontSemibold = makeFont(ds->dpi, 9, FW_SEMIBOLD);
    ds->fontSmall = makeFont(ds->dpi, 8, FW_NORMAL);        // counts and badge (11 px)
    ds->brBody = CreateSolidBrush(col::body);
    ds->brBand = CreateSolidBrush(col::band);
    ds->brEdit = CreateSolidBrush(col::editBg);
}

void destroyResources(DialogState* ds) {
    for (HFONT* f : {&ds->font, &ds->fontSemibold, &ds->fontSmall})
        if (*f) { DeleteObject(*f); *f = nullptr; }
    for (HBRUSH* b : {&ds->brBody, &ds->brBand, &ds->brEdit})
        if (*b) { DeleteObject(*b); *b = nullptr; }
}

bool isCountLabel(int id) {
    for (int c : kCountedIds)
        if (id == c + IDC_CNT_OFFSET) return true;
    return false;
}

// ============================================================================
// Dialog Procedure
// ============================================================================

BOOL onInitDialog(HWND hDlg, DialogState* ds) {
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ds));
    wdx::ApplyWindowIcon(hDlg, IDI_WHITEOUTDEX_ICON);
    applyDarkCaption(hDlg);

    HDC hdc = GetDC(hDlg);
    ds->dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(hDlg, hdc);
    if (ds->dpi <= 0) ds->dpi = 96;
    createResources(ds);

    // Relabel from the catalog before anything measures or reads a caption.
    wdx::l10n::LocalizeDialog(hDlg, kImportStrings);
    wdx::l10n::LocalizeRadioGroup(hDlg, "imp_mode_labels", kModeIds);
    const DrawnText t = loadDrawnText();
    SetWindowTextW(hDlg, ds->file->isReforged ? t.titleReforged.c_str() : t.title.c_str());
    ds->subNewScene = t.subNew;
    ds->subMerge = t.subMerge;
    initPresetCombo(hDlg, t.presets);
    buildHeaderAndFooter(ds, t);

    // Fonts: regular for every control, small for the counts
    for (HWND c = GetWindow(hDlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(isCountLabel(GetDlgCtrlID(c)) ? ds->fontSmall : ds->font),
                     FALSE);

    // Counts from the parsed file
    for (int id : kCountedIds)
        SetDlgItemInt(hDlg, id + IDC_CNT_OFFSET, static_cast<UINT>(std::max(0, countFor(*ds->file, id))), FALSE);

    // Saved settings, then: nothing to import -> unchecked; a Fast Setting
    // other than Custom decides the checkboxes, exactly as the import will
    // (the plug-in runs applyFastPreset afterwards).
    ds->updating = true;
    optionsToDialog(hDlg, ds);
    for (int id : kFastIds)
        if (!present(*ds->file, id)) setCheck(hDlg, id, false);
    if (ds->preset != 0)
        applyPreset(hDlg, ds, ds->preset);
    SendDlgItemMessageW(hDlg, IDC_CMB_FAST_SETTINGS, CB_SETCURSEL, ds->preset, 0);
    ds->updating = false;

    for (int id : kOwnerDrawn)
        installHover(hDlg, id);

    layoutImportDialog(hDlg, ds);
    updateTabStops(hDlg, ds);
    applyPageVisibility(hDlg, ds);
    applyEnabledState(hDlg, ds);

    CenterWindow(hDlg, GetParent(hDlg));
    return TRUE;
}

// A checkbox the user clicked: dependencies, enabled state, preset check.
void onCheckboxClicked(HWND hDlg, DialogState* ds, int id) {
    if (ds->updating) return;
    ds->updating = true;
    const ImportFileInfo& f = *ds->file;

    // Master switches take their rows along (as before the redesign).
    if (id == IDC_CHK_IMPORT_OBJECTS) {
        const bool on = getCheck(hDlg, id);
        for (int row : kObjectRows)
            setCheck(hDlg, row, on && present(f, row));
    } else if (id == IDC_CHK_IMPORT_ANIMATIONS) {
        const bool on = getCheck(hDlg, id);
        for (int row : kAnimRows)
            setCheck(hDlg, row, on && present(f, row));
    } else if (id == IDC_CHK_BONES && !getCheck(hDlg, IDC_CHK_BONES)) {
        setCheck(hDlg, IDC_CHK_SKINNED, false);  // no skin without bones
    }

    applyEnabledState(hDlg, ds);
    refreshPresetAfterEdit(hDlg, ds);
    ds->updating = false;
}

INT_PTR CALLBACK ImportDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DialogState* ds = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG:
        return onInitDialog(hDlg, reinterpret_cast<DialogState*>(lParam));

    case WM_ERASEBKGND: {
        if (!ds) break;
        // Our own erase is skipped: WM_PAINT fills the whole client area, and
        // erasing first would only flicker. But themed checkboxes draw a
        // transparent background by asking the parent to paint into THEIR DC
        // (DrawThemeParentBackground: WM_ERASEBKGND, then WM_PRINTCLIENT).
        // Same as the export dialog.
        HDC dc = reinterpret_cast<HDC>(wParam);
        if (dc && WindowFromDC(dc) != hDlg)
            drawDialogInto(hDlg, ds, dc);
        SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, TRUE);
        return TRUE;
    }

    // Not handled by DefWindowProc: a window has to draw its client area
    // into the given DC itself (learn.microsoft.com, WM_PRINTCLIENT).
    case WM_PRINTCLIENT:
        if (!ds) break;
        if (wParam)
            drawDialogInto(hDlg, ds, reinterpret_cast<HDC>(wParam));
        return TRUE;

    case WM_PAINT:
        if (!ds) break;
        paintDialog(hDlg, ds);
        return TRUE;

    case WM_CTLCOLORDLG:
        if (!ds) break;
        return reinterpret_cast<INT_PTR>(ds->brBody);

    case WM_CTLCOLORSTATIC: {
        if (!ds) break;
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND ctl = reinterpret_cast<HWND>(lParam);
        const int id = GetDlgCtrlID(ctl);
        const bool enabled = IsWindowEnabled(ctl) != FALSE;
        COLORREF c = col::text;
        if (id == IDC_LBL_FAST_SETTINGS || id == IDC_LBL_MODE || id == IDC_LBL_OPTIONS || isCountLabel(id))
            c = col::textDim;
        if (id == IDC_LBL_NO_OBJECTS || id == IDC_LBL_NO_ANIMATIONS || !enabled)
            c = col::textDisabled;
        SetTextColor(dc, c);
        SetBkColor(dc, col::body);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<INT_PTR>(ds->brBody);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        if (!ds) break;
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, col::editText);
        SetBkColor(dc, col::editBg);
        return reinterpret_cast<INT_PTR>(ds->brEdit);
    }

    case WM_CTLCOLORBTN: {
        if (!ds) break;
        HDC dc = reinterpret_cast<HDC>(wParam);
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, col::text);
        return reinterpret_cast<INT_PTR>((id == IDCANCEL || id == IDOK) ? ds->brBand : ds->brBody);
    }

    case WM_DRAWITEM: {
        if (!ds) break;
        const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (!dis || dis->CtlType != ODT_BUTTON) break;
        switch (dis->CtlID) {
        case IDC_BTN_MODE_NEW_SCENE: drawModeCard(dis, ds, false); return TRUE;
        case IDC_BTN_MODE_MERGE:     drawModeCard(dis, ds, true); return TRUE;
        case IDC_BTN_TAB_OBJECTS:    drawTab(dis, ds, 0); return TRUE;
        case IDC_BTN_TAB_ANIMATIONS: drawTab(dis, ds, 1); return TRUE;
        case IDC_BTN_TAB_MODEL:      drawTab(dis, ds, 2); return TRUE;
        case IDOK:                   drawImportButton(dis, ds); return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        if (!ds) break;
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        const bool clicked = code == BN_CLICKED || code == BN_DBLCLK;

        switch (id) {
        // Both end ImportDialog::run's loop; the window itself stays until
        // ImportDialog::close.
        case IDOK:
            if (code != BN_CLICKED || ds->done) return TRUE;
            dialogToOptions(hDlg, ds, *ds->opts);
            saveDialogSettingsToINI(hDlg, ds);
            ds->confirmed = true;
            ds->done = true;
            return TRUE;

        case IDCANCEL:
            if (ds->done) return TRUE;
            ds->confirmed = false;
            ds->done = true;
            return TRUE;

        case IDC_CMB_FAST_SETTINGS:
            if (code == CBN_SELCHANGE) {
                const int sel = static_cast<int>(SendDlgItemMessageW(hDlg, id, CB_GETCURSEL, 0, 0));
                ds->preset = std::min(5, std::max(0, sel));
                ds->updating = true;
                applyPreset(hDlg, ds, ds->preset);   // Custom leaves everything as it is
                applyEnabledState(hDlg, ds);
                ds->updating = false;
            }
            return TRUE;

        // Cards and tabs select on click and when the arrow keys move focus
        // onto them (BN_SETFOCUS needs BS_NOTIFY, set in the .rc).
        case IDC_BTN_MODE_NEW_SCENE:
        case IDC_BTN_MODE_MERGE:
            if (clicked || code == BN_SETFOCUS)
                selectMode(hDlg, ds, id == IDC_BTN_MODE_MERGE);
            return TRUE;

        case IDC_BTN_TAB_OBJECTS:
        case IDC_BTN_TAB_ANIMATIONS:
        case IDC_BTN_TAB_MODEL:
            if (clicked || code == BN_SETFOCUS)
                selectTab(hDlg, ds, id == IDC_BTN_TAB_OBJECTS ? 0 : (id == IDC_BTN_TAB_ANIMATIONS ? 1 : 2));
            return TRUE;

        default:
            if (code == BN_CLICKED &&
                std::find(std::begin(kFastIds), std::end(kFastIds), id) != std::end(kFastIds)) {
                onCheckboxClicked(hDlg, ds, id);
                return TRUE;
            }
            break;
        }
        break;
    }

    case WM_CLOSE:
        // No closing while the import runs; before that, closing is Cancel.
        if (ds && !ds->done) {
            ds->confirmed = false;
            ds->done = true;
        }
        return TRUE;
    }

    return FALSE;
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

struct ImportDialogImpl {
    HINSTANCE hInstance = nullptr;
    HWND parent = nullptr;
    HWND hDlg = nullptr;
    bool parentDisabled = false;   // disabled by run(), enabled again by close()
    ULONG_PTR gdiplusToken = 0;
    ImportFileInfo file;           // the dialog paints from it until close()
    DialogState ds;
};

ImportDialog::ImportDialog(HINSTANCE hInstance, HWND hWndParent)
    : impl_(std::make_unique<ImportDialogImpl>()) {
    impl_->hInstance = hInstance;
    impl_->parent = hWndParent;
}

ImportDialog::~ImportDialog() {
    close();
}

// A modal dialog built from a modeless one: DialogBox would destroy the window
// when the user clicks Import, and the window has to stay for the progress.
// Like DialogBox, this disables the owner while the window exists and runs
// its own message loop, handing a WM_QUIT back to the application's loop.
bool ImportDialog::run(MdlxImportOptions& opts, const ImportFileInfo& file) {
    ImportDialogImpl& m = *impl_;
    if (m.hDlg) return false;

    m.file = file;
    m.ds = DialogState{};
    m.ds.opts = &opts;
    m.ds.file = &m.file;

    // GDI+ draws the anti-aliased shapes; it must not be started in DllMain.
    Gdiplus::GdiplusStartupInput gdiplusInput;
    g_gdiplus = Gdiplus::GdiplusStartup(&m.gdiplusToken, &gdiplusInput, nullptr) == Gdiplus::Ok;

    m.hDlg = CreateDialogParamW(m.hInstance, MAKEINTRESOURCEW(IDD_IMPORT_OPTIONS), m.parent,
                                ImportDialogProc, reinterpret_cast<LPARAM>(&m.ds));
    if (!m.hDlg) {
        close();
        return false;
    }
    if (m.parent && IsWindowEnabled(m.parent)) {
        EnableWindow(m.parent, FALSE);
        m.parentDisabled = true;
    }
    ShowWindow(m.hDlg, SW_SHOW);

    MSG msg;
    while (!m.ds.done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) {
            PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (r == -1) break;
        if (!IsDialogMessageW(m.hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (!m.ds.confirmed) {
        close();
        return false;
    }
    return true;
}

void ImportDialog::step(ImportStep s) {
    ImportDialogImpl& m = *impl_;
    const size_t i = static_cast<size_t>(s);
    if (!m.hDlg || !m.ds.confirmed || i >= std::size(kSteps)) return;
    if (!m.ds.busy) beginProgress(m.hDlg, &m.ds);
    m.ds.step = static_cast<int>(i);
    m.ds.itemsDone = m.ds.itemsTotal = 0;
    m.ds.progress = kSteps[i].from;
    refreshProgress(m.hDlg, &m.ds, true);
}

void ImportDialog::items(size_t done, size_t total) {
    ImportDialogImpl& m = *impl_;
    if (!m.hDlg || !m.ds.busy || m.ds.step < 0 || total == 0) return;
    const StepInfo& s = kSteps[static_cast<size_t>(m.ds.step)];
    m.ds.itemsDone = done;
    m.ds.itemsTotal = total;
    m.ds.progress = s.from + (s.to - s.from) * static_cast<float>(std::min(done, total)) / static_cast<float>(total);
    refreshProgress(m.hDlg, &m.ds, false);
}

void ImportDialog::close() {
    ImportDialogImpl& m = *impl_;
    // Owner first, as when a modal dialog ends: the owner is enabled again
    // before the window goes, so activation returns to Max.
    if (m.parentDisabled) {
        EnableWindow(m.parent, TRUE);
        m.parentDisabled = false;
    }
    if (m.hDlg) {
        DestroyWindow(m.hDlg);
        m.hDlg = nullptr;
    }
    destroyResources(&m.ds);
    if (g_gdiplus) {
        Gdiplus::GdiplusShutdown(m.gdiplusToken);
        g_gdiplus = false;
    }
}
