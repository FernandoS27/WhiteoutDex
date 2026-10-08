// MDLXExporter — Export options dialog implementation
//
// Redesign, September 2026 (WhiteoutDex_ExportDialog_Redesign_Spec.md):
//   header band with the target file, format cards, two tabs (Geometry,
//   Textures), a scene-check banner and a footer with the file options.
//   After Export the window stays open and its footer shows the progress of
//   the export (ExportDialog::step / items) until DoExport closes it.
//
// Layout values are pixels at 96 DPI, scaled by the dialog's DPI at runtime.
// The .rc only defines controls, styles and tab order. Everything is plain
// Win32 plus GDI+ for anti-aliased shapes, so one code path serves 3ds Max
// 2016 through 2027.
#include "export_dialog.h"
#include "export_paths.h"
#include "resource.h"
#include "scene_monitor.h"

#include <max.h>
#include <MaxDirectories.h>
#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>
#include <cstdio>
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

// Provided by dllmain.cpp — we need this to launch the Problem Details dialog
extern HINSTANCE GetDllInstance();

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

namespace {

// ============================================================================
// Design tokens (spec section 4)
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
constexpr COLORREF warnBg       = RGB(0x4D, 0x44, 0x31);
constexpr COLORREF warnBorder   = RGB(0x5E, 0x52, 0x38);
constexpr COLORREF warnIcon     = RGB(0xF0, 0xB2, 0x3E);
constexpr COLORREF okBg         = RGB(0x39, 0x4A, 0x3B);
constexpr COLORREF okBorder     = RGB(0x4A, 0x5D, 0x4C);
constexpr COLORREF okIcon       = RGB(0x71, 0xC1, 0x77);
constexpr COLORREF bannerTitle  = RGB(0xF0, 0xF0, 0xF0);
constexpr COLORREF bannerSub    = RGB(0xC8, 0xC8, 0xC8);
constexpr COLORREF iconInk      = RGB(0x2B, 0x2B, 0x2B);
constexpr COLORREF okInk        = RGB(0x1D, 0x2A, 0x1E);
constexpr COLORREF smallBtn     = RGB(0x5B, 0x53, 0x47);
constexpr COLORREF smallBtnHov  = RGB(0x68, 0x5F, 0x51);
constexpr COLORREF btnText      = RGB(0xEC, 0xEC, 0xEC);
constexpr COLORREF primary      = RGB(0x00, 0x78, 0xD7);
constexpr COLORREF primaryHover = RGB(0x1A, 0x86, 0xDD);
constexpr COLORREF primaryDown  = RGB(0x00, 0x67, 0xB8);
constexpr COLORREF primaryText  = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF checkBox     = RGB(0x64, 0x64, 0x64);
constexpr COLORREF checkBoxHov  = RGB(0x6E, 0x6E, 0x6E);
constexpr COLORREF checkMark    = RGB(0xEC, 0xEC, 0xEC);
constexpr COLORREF focus        = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF docPage      = RGB(0xD9, 0xD9, 0xD9);
constexpr COLORREF docFold      = RGB(0xA8, 0xA8, 0xA8);
constexpr COLORREF docLines     = RGB(0x6B, 0x6B, 0x6B);
} // namespace col

// ============================================================================
// Layout (spec section 3): client area 362 x 493 px at 96 DPI
// ============================================================================

struct Px { int x, y, w, h; };

namespace lay {
constexpr int clientW = 362;
constexpr int clientH = 493;

constexpr int headerH = 54;             // band 0..53, separator row at y = 53
constexpr int headerLineY = 53;
constexpr Px  docIcon{11, 10, 32, 32};
constexpr Px  fileName{53, 9, 298, 16};
constexpr Px  fileNote{53, 27, 298, 16};

constexpr Px  lblModel{11, 65, 340, 16};
constexpr Px  edtModel{11, 86, 340, 23};
constexpr Px  lblFormat{11, 120, 340, 16};
constexpr Px  cardClassic{11, 141, 167, 42};
constexpr Px  cardReforged{185, 141, 167, 42};

constexpr int tabX = 11, tabY = 194, tabH = 24, tabGap = 22;
constexpr int underlineY = 218, underlineH = 2;
constexpr Px  tabStrip{11, 219, 340, 1};

// Geometry tab
constexpr Px  chkMerge{11, 231, 340, 17};
constexpr Px  chkNormals{11, 255, 340, 17};
constexpr Px  chkKeep{11, 279, 340, 17};
constexpr Px  chkQuantize{11, 303, 340, 17};
constexpr Px  lblPrecision{11, 331, 120, 16};
constexpr Px  edtPrecision{138, 327, 44, 23};
constexpr Px  spnPrecision{182, 327, 14, 23};

// Textures tab
constexpr Px  chkConvert{11, 231, 340, 17};
constexpr Px  lblTexRow1{30, 259, 104, 16};   // "Compression" (Classic) / "DDS format" (Reforged)
constexpr Px  cmbTexRow1{141, 255, 210, 23};
constexpr Px  chkDither{30, 285, 321, 17};    // Classic + Paletted
constexpr Px  lblJpeg{30, 289, 104, 16};      // Classic + JPEG
constexpr Px  edtJpeg{141, 285, 44, 23};
constexpr Px  spnJpeg{185, 285, 14, 23};
constexpr int subX = 30, subW = 321, rowH = 17, rowPitch = 24;
constexpr int mipYPaletted = 309, mipYJpeg = 315, mipYReforged = 285;
constexpr int comboDropH = 160;

// Scene check banner
constexpr Px  banner{11, 367, 340, 54};
constexpr Px  bannerIcon{21, 378, 16, 16};
constexpr int bannerTextX = 44, bannerTitleY = 378, bannerTextH = 16;
constexpr Px  bannerSub{44, 397, 298, 16};
constexpr int bannerBtnY = 375, bannerBtnH = 21, bannerBtnRight = 342;
constexpr int bannerBtnGap = 6, bannerBtnPad = 9;

// Footer
constexpr int footerY = 432;            // separator row at y = 432
constexpr int footerChkX = 11, footerChk1Y = 443, footerChk2Y = 466;
constexpr int footerBtnY = 460, footerBtnH = 23, footerRight = 351;
constexpr int footerBtnGap = 7, footerBtnMinW = 75, footerBtnPad = 12;
// Progress (after Export), replacing the check buttons and buttons
constexpr int progTextY = 446, progTextH = 16;
constexpr int progBarY = 469, progBarH = 6;
} // namespace lay

// ============================================================================
// Export steps shown in the footer while the export runs
// ============================================================================

// Text and share of the bar (from, to in 0..1) of each ExportStep, in enum
// order. Baking the animations takes longest on a typical animated scene,
// then reading the meshes and converting textures.
struct StepInfo {
    const char* key;
    const wchar_t* english;
    float from, to;
};

constexpr StepInfo kSteps[] = {
    {"exp_progress_scene_lbl",      L"Reading the scene…",               0.00f, 0.05f},
    {"exp_progress_bones_lbl",      L"Collecting bones…",                0.05f, 0.10f},
    {"exp_progress_meshes_lbl",     L"Reading meshes…",                  0.10f, 0.30f},
    {"exp_progress_objects_lbl",    L"Reading materials and objects…",   0.30f, 0.40f},
    {"exp_progress_animations_lbl", L"Baking animations…",               0.40f, 0.80f},
    {"exp_progress_optimizing_lbl", L"Optimizing…",                      0.80f, 0.85f},
    {"exp_progress_textures_lbl",   L"Converting textures…",             0.85f, 0.93f},
    {"exp_progress_building_lbl",   L"Building the model…",              0.93f, 0.97f},
    {"exp_progress_writing_lbl",    L"Writing the file…",                0.97f, 1.00f},
};
static_assert(std::size(kSteps) == static_cast<size_t>(ExportStep::Count),
              "one kSteps entry per ExportStep");

// ============================================================================
// Dialog state passed via LPARAM → GWLP_USERDATA
// ============================================================================

struct DialogState {
    MdxExportOptions* opts = nullptr;
    bool confirmed = false;
    bool done = false;           // Export or Cancel chosen: ExportDialog::run leaves its loop

    // Progress, after Export: the footer shows it while the export runs.
    bool busy = false;
    std::vector<std::wstring> stepText;   // per ExportStep (catalog, English fallback)
    int    step = -1;
    size_t itemsDone = 0, itemsTotal = 0;
    float  progress = 0.0f;               // 0..1 of the whole export
    ULONGLONG lastPaint = 0;              // GetTickCount64 of the last footer repaint

    // Empty when the file is chosen after the dialog (mdxExport).
    std::wstring targetPath;

    bool reforged = false;
    int  tab = 0;                // 0 = Geometry, 1 = Textures
    bool autoIncrement = false;  // footer check buttons are owner-drawn,
    bool openFolder = false;     // so their state lives here

    // Scene check
    int total = 0, names = 0, meshes = 0, controllers = 0, materials = 0;

    // Header text, recomputed when the target or Auto-increment changes
    std::wstring headerLine1, headerLine2;
    bool headerWarn = false;

    // Device-pixel rectangles the dialog paints around
    RECT tabRect[2]{};
    int  fixAllLeft = 0;

    // Resources
    int    dpi = 96;
    HFONT  font = nullptr;
    HFONT  fontSemibold = nullptr;
    HBRUSH brBody = nullptr, brBand = nullptr, brEdit = nullptr, brWarn = nullptr;

    // Drawn strings (catalog, English fallback)
    std::wstring subClassic, subReforged;
    std::wstring targetNew, targetIncrementFmt, targetOverwrite;
    std::wstring targetPendingTitle, targetPendingNote;
    std::wstring bannerOneFmt, bannerManyFmt, bannerSummaryFmt, bannerNone, bannerChecked;

    int   S(int v) const { return MulDiv(v, dpi, 96); }
    float Sf(float v) const { return v * static_cast<float>(dpi) / 96.0f; }
    RECT  R(const Px& p) const { return RECT{S(p.x), S(p.y), S(p.x + p.w), S(p.y + p.h)}; }
};

bool g_gdiplus = false;  // GDI+ started by ExportDialog::run while the window exists

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

int comboSel(HWND hDlg, int id) {
    return static_cast<int>(SendDlgItemMessageW(hDlg, id, CB_GETCURSEL, 0, 0));
}

// SWP_NOCOPYBITS: without it Windows copies the control's old pixels to the
// new position, and a checkbox with a transparent (themed) background then
// paints its text over that copy.
void place(HWND hDlg, const DialogState* ds, int id, const Px& r) {
    if (HWND h = GetDlgItem(hDlg, id))
        SetWindowPos(h, nullptr, ds->S(r.x), ds->S(r.y), ds->S(r.w), ds->S(r.h),
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

void placeDevice(HWND hDlg, int id, int x, int y, int w, int h) {
    if (HWND c = GetDlgItem(hDlg, id))
        SetWindowPos(c, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

// A drop-down list's window height is its selection field; the height passed
// to SetWindowPos also sizes the list. Set the field to |r.h| and centre it if
// the control rounds the height.
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

// Replace each "%d" in order. Catalog format strings are never passed to
// printf, so a translation cannot corrupt the stack.
std::wstring fillInts(std::wstring fmt, std::initializer_list<int> values) {
    for (int v : values) {
        const size_t at = fmt.find(L"%d");
        if (at == std::wstring::npos) break;
        fmt.replace(at, 2, std::to_wstring(v));
    }
    return fmt;
}

int countToken(const std::wstring& s, const wchar_t* token) {
    int n = 0;
    const size_t len = wcslen(token);
    for (size_t at = s.find(token); at != std::wstring::npos; at = s.find(token, at + len))
        ++n;
    return n;
}

std::wstring fileNameOf(const std::wstring& path) {
    const size_t sl = path.find_last_of(L"\\/");
    return (sl == std::wstring::npos) ? path : path.substr(sl + 1);
}

// ============================================================================
// Shapes: GDI+ when available (anti-aliased), plain GDI otherwise
// ============================================================================

Gdiplus::Color gp(COLORREF c) {
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

// Anti-aliased, with pixel i covering [i, i+1] (PixelOffsetModeHalf) - the
// same convention as the SVG mockup. Rectangles at integer bounds then fill
// whole pixels, and a 1 px border centred on x + 0.5 lands on column x.
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
// rather than a pen. Fills at whole-pixel bounds are exact with any
// rasteriser, so the border lands on the outermost pixel columns and rows.
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

// Icons from the spec appendix (16 px design box, document glyph 32 px).
void drawWarnIcon(HDC dc, const RECT& r) {
    const float x = static_cast<float>(r.left), y = static_cast<float>(r.top);
    const float s = static_cast<float>(r.right - r.left);
    fillPolygon(dc, x, y, s, 16.0f, {{8.0f, 1.6f}, {15.2f, 14.4f}, {0.8f, 14.4f}}, col::warnIcon);
    strokePolyline(dc, x, y, s, 16.0f, {{8.0f, 5.9f}, {8.0f, 10.0f}}, 1.7f, col::iconInk);
    fillEllipse(dc, x + 8.0f * s / 16.0f, y + 12.2f * s / 16.0f, 1.0f * s / 16.0f, col::iconInk);
}

void drawOkIcon(HDC dc, const RECT& r) {
    const float x = static_cast<float>(r.left), y = static_cast<float>(r.top);
    const float s = static_cast<float>(r.right - r.left);
    fillEllipse(dc, x + 8.0f * s / 16.0f, y + 8.0f * s / 16.0f, 7.2f * s / 16.0f, col::okIcon);
    strokePolyline(dc, x, y, s, 16.0f, {{4.7f, 8.3f}, {7.0f, 10.5f}, {11.3f, 5.9f}}, 1.8f, col::okInk);
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

const wchar_t kPropOldProc[] = L"WdxExportOldProc";
const wchar_t kPropHover[]   = L"WdxExportHover";

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
    // Store the old procedure before switching, so no message can reach
    // hoverButtonProc without it.
    SetPropW(h, kPropOldProc, reinterpret_cast<HANDLE>(GetWindowLongPtrW(h, GWLP_WNDPROC)));
    SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hoverButtonProc));
}

// ============================================================================
// Control groups
// ============================================================================

constexpr int kGeometryControls[] = {
    IDC_CHK_MERGE_SIMILAR, IDC_CHK_FIX_SHARED_NORMALS, IDC_CHK_KEEP_UNUSED_BH, IDC_CHK_QUANTIZE_SKIN,
    IDC_LBL_EXTENTS_PREC,  IDC_EDT_EXTENTS_PREC,       IDC_SPIN_EXTENTS_PREC,
};

constexpr int kTextureSubControls[] = {
    IDC_LBL_BLP_COMPRESSION, IDC_CMB_BLP_COMPRESSION, IDC_LBL_BLP_JPEG_QUALITY, IDC_EDT_BLP_JPEG_QUALITY,
    IDC_SPIN_BLP_JPEG_QUALITY, IDC_CHK_BLP_DITHERING, IDC_LBL_DDS_FORMAT, IDC_CMB_DDS_FORMAT,
    IDC_CHK_TEX_MIPMAPS, IDC_CHK_TEX_OVERWRITE,
};

constexpr int kOwnerDrawn[] = {
    IDC_BTN_FORMAT_CLASSIC, IDC_BTN_FORMAT_REFORGED, IDC_BTN_TAB_GEOMETRY, IDC_BTN_TAB_TEXTURES,
    IDC_BTN_SCENE_FIX_ALL,  IDC_BTN_SCENE_DETAILS,   IDC_CHK_AUTO_INCREMENT, IDC_CHK_OPEN_FOLDER, IDOK,
};

constexpr int kAllControls[] = {
    IDC_LBL_MODEL_NAME, IDC_EDT_MODEL_NAME, IDC_LBL_FORMAT, IDC_BTN_FORMAT_CLASSIC, IDC_BTN_FORMAT_REFORGED,
    IDC_BTN_TAB_GEOMETRY, IDC_BTN_TAB_TEXTURES, IDC_CHK_MERGE_SIMILAR, IDC_CHK_FIX_SHARED_NORMALS,
    IDC_CHK_KEEP_UNUSED_BH, IDC_CHK_QUANTIZE_SKIN, IDC_LBL_EXTENTS_PREC, IDC_EDT_EXTENTS_PREC,
    IDC_SPIN_EXTENTS_PREC, IDC_CHK_TEX_CONVERT, IDC_LBL_BLP_COMPRESSION, IDC_CMB_BLP_COMPRESSION,
    IDC_LBL_BLP_JPEG_QUALITY, IDC_EDT_BLP_JPEG_QUALITY, IDC_SPIN_BLP_JPEG_QUALITY, IDC_CHK_BLP_DITHERING,
    IDC_LBL_DDS_FORMAT, IDC_CMB_DDS_FORMAT, IDC_CHK_TEX_MIPMAPS, IDC_CHK_TEX_OVERWRITE,
    IDC_BTN_SCENE_FIX_ALL, IDC_BTN_SCENE_DETAILS, IDC_CHK_AUTO_INCREMENT, IDC_CHK_OPEN_FOLDER, IDOK, IDCANCEL,
};

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

// Same keys as before the redesign (the macroscript reads them too). The
// active tab moved to its own key: "LastTab" used 0/1/2 with 1 = Material Fix,
// which would clash with the new 1 = Textures.
void saveDialogSettingsToINI(HWND hDlg, const DialogState* ds) {
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

    wchar_t nameText[256];
    GetDlgItemTextW(hDlg, IDC_EDT_MODEL_NAME, nameText, 256);
    writeS(L"ModelName", nameText);

    // Format Version (macroscript: 1=Classic, 2=Reforged)
    writeI(L"ExportVersion", ds->reforged ? 2 : 1);

    writeB(L"MergeSimilarMeshes", getCheck(hDlg, IDC_CHK_MERGE_SIMILAR));
    writeB(L"FixSharedNormals", getCheck(hDlg, IDC_CHK_FIX_SHARED_NORMALS));
    writeB(L"KeepUnusedBonesHelpers", getCheck(hDlg, IDC_CHK_KEEP_UNUSED_BH));
    // The checkbox is the positive "Quantize skin weights"; the key keeps its old meaning.
    writeB(L"DisableSkinQuantize", !getCheck(hDlg, IDC_CHK_QUANTIZE_SKIN));
    writeB(L"AutoIncrementFilename", ds->autoIncrement);
    writeB(L"OpenFolderAfterExport", ds->openFolder);

    writeB(L"TexConvertEnabled",    getCheck(hDlg, IDC_CHK_TEX_CONVERT));
    writeB(L"TexGenerateMipmaps",   getCheck(hDlg, IDC_CHK_TEX_MIPMAPS));
    writeB(L"TexOverwriteExisting", getCheck(hDlg, IDC_CHK_TEX_OVERWRITE));

    writeI(L"BlpCompression", std::max(0, comboSel(hDlg, IDC_CMB_BLP_COMPRESSION)));
    wchar_t jpegText[16];
    GetDlgItemTextW(hDlg, IDC_EDT_BLP_JPEG_QUALITY, jpegText, 16);
    writeS(L"BlpJpegQuality", jpegText);
    writeB(L"BlpDithering", getCheck(hDlg, IDC_CHK_BLP_DITHERING));
    writeI(L"DdsFormat", std::max(0, comboSel(hDlg, IDC_CMB_DDS_FORMAT)));

    writeI(L"ExportDialogTab", ds->tab);

    wchar_t precText[16];
    GetDlgItemTextW(hDlg, IDC_EDT_EXTENTS_PREC, precText, 16);
    writeS(L"ExtentsPrecision", precText);
}

void loadDialogSettingsFromINI(HWND hDlg, DialogState* ds) {
    std::wstring iniPath = getINIPath();
    if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;

    auto gs = [&](const wchar_t* key) {
        return iniGetString(iniPath.c_str(), L"Settings", key);
    };
    auto loadCheck = [&](const wchar_t* key, int id) {
        const std::wstring v = gs(key);
        if (!v.empty()) setCheck(hDlg, id, iniBool(v));
    };

    if (auto modelName = gs(L"ModelName"); !modelName.empty())
        SetDlgItemTextW(hDlg, IDC_EDT_MODEL_NAME, modelName.c_str());

    if (auto ver = gs(L"ExportVersion"); !ver.empty())
        ds->reforged = (_wtoi(ver.c_str()) == 2);

    loadCheck(L"MergeSimilarMeshes", IDC_CHK_MERGE_SIMILAR);
    loadCheck(L"FixSharedNormals", IDC_CHK_FIX_SHARED_NORMALS);
    loadCheck(L"KeepUnusedBonesHelpers", IDC_CHK_KEEP_UNUSED_BH);
    if (auto noSkQ = gs(L"DisableSkinQuantize"); !noSkQ.empty())
        setCheck(hDlg, IDC_CHK_QUANTIZE_SKIN, !iniBool(noSkQ));
    if (auto autoI = gs(L"AutoIncrementFilename"); !autoI.empty())
        ds->autoIncrement = iniBool(autoI);
    if (auto openF = gs(L"OpenFolderAfterExport"); !openF.empty())
        ds->openFolder = iniBool(openF);

    loadCheck(L"TexConvertEnabled", IDC_CHK_TEX_CONVERT);
    loadCheck(L"TexGenerateMipmaps", IDC_CHK_TEX_MIPMAPS);
    loadCheck(L"TexOverwriteExisting", IDC_CHK_TEX_OVERWRITE);

    if (auto blpComp = gs(L"BlpCompression"); !blpComp.empty())
        SendDlgItemMessageW(hDlg, IDC_CMB_BLP_COMPRESSION, CB_SETCURSEL, _wtoi(blpComp.c_str()), 0);
    if (auto jpegQ = gs(L"BlpJpegQuality"); !jpegQ.empty())
        SetDlgItemTextW(hDlg, IDC_EDT_BLP_JPEG_QUALITY, jpegQ.c_str());
    loadCheck(L"BlpDithering", IDC_CHK_BLP_DITHERING);
    if (auto ddsFmt = gs(L"DdsFormat"); !ddsFmt.empty())
        SendDlgItemMessageW(hDlg, IDC_CMB_DDS_FORMAT, CB_SETCURSEL, _wtoi(ddsFmt.c_str()), 0);

    // Active tab. Settings written before the redesign only have "LastTab"
    // (0 = Options, 1 = Material Fix, 2 = Texture Conversion).
    if (auto tab = gs(L"ExportDialogTab"); !tab.empty())
        ds->tab = (_wtoi(tab.c_str()) == 1) ? 1 : 0;
    else if (auto lastTab = gs(L"LastTab"); !lastTab.empty())
        ds->tab = (_wtoi(lastTab.c_str()) == 2) ? 1 : 0;

    if (auto extPrec = gs(L"ExtentsPrecision"); !extPrec.empty())
        SetDlgItemTextW(hDlg, IDC_EDT_EXTENTS_PREC, extPrec.c_str());
}

// ── Options struct <-> dialog ──────────────────────────────

void dialogToOptions(HWND hDlg, const DialogState* ds, MdxExportOptions& opts) {
    wchar_t nameText[256];
    GetDlgItemTextW(hDlg, IDC_EDT_MODEL_NAME, nameText, 256);
    int len = WideCharToMultiByte(CP_UTF8, 0, nameText, -1, nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        opts.modelName.resize(static_cast<size_t>(len - 1));
        WideCharToMultiByte(CP_UTF8, 0, nameText, -1, &opts.modelName[0], len, nullptr, nullptr);
    }

    // Format Version. Reforged is written as v1800, the version WC3 3.0.0 ships.
    opts.version = ds->reforged ? 1800 : 800;

    opts.mergeGeosets = getCheck(hDlg, IDC_CHK_MERGE_SIMILAR);
    opts.fixSharedNormals = getCheck(hDlg, IDC_CHK_FIX_SHARED_NORMALS);
    opts.keepUnusedBonesHelpers = getCheck(hDlg, IDC_CHK_KEEP_UNUSED_BH);
    opts.disableSkinQuantize = !getCheck(hDlg, IDC_CHK_QUANTIZE_SKIN);
    opts.autoIncrementFilename = ds->autoIncrement;
    opts.openFolderAfterExport = ds->openFolder;
    // exportSmoothgroups is forced true via struct default; no UI binding
    // extentsType is forced 1 (animation-dependent) via struct default; no UI binding

    opts.texConvertEnabled    = getCheck(hDlg, IDC_CHK_TEX_CONVERT);
    opts.texGenerateMipmaps   = getCheck(hDlg, IDC_CHK_TEX_MIPMAPS);
    opts.texOverwriteExisting = getCheck(hDlg, IDC_CHK_TEX_OVERWRITE);

    opts.blpCompression = std::max(0, comboSel(hDlg, IDC_CMB_BLP_COMPRESSION));
    wchar_t jpegText[16];
    GetDlgItemTextW(hDlg, IDC_EDT_BLP_JPEG_QUALITY, jpegText, 16);
    opts.blpJpegQuality = _wtoi(jpegText);
    opts.blpDithering = getCheck(hDlg, IDC_CHK_BLP_DITHERING);
    opts.ddsFormat = std::max(0, comboSel(hDlg, IDC_CMB_DDS_FORMAT));

    wchar_t precText[16];
    GetDlgItemTextW(hDlg, IDC_EDT_EXTENTS_PREC, precText, 16);
    opts.extentsPrecision = _wtoi(precText);
}

void optionsToDialog(HWND hDlg, DialogState* ds, const MdxExportOptions& opts) {
    if (!opts.modelName.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, opts.modelName.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wname(static_cast<size_t>(wlen - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, opts.modelName.c_str(), -1, &wname[0], wlen);
            SetDlgItemTextW(hDlg, IDC_EDT_MODEL_NAME, wname.c_str());
        }
    }

    ds->reforged = opts.version >= 1200;

    setCheck(hDlg, IDC_CHK_MERGE_SIMILAR, opts.mergeGeosets);
    setCheck(hDlg, IDC_CHK_FIX_SHARED_NORMALS, opts.fixSharedNormals);
    setCheck(hDlg, IDC_CHK_KEEP_UNUSED_BH, opts.keepUnusedBonesHelpers);
    setCheck(hDlg, IDC_CHK_QUANTIZE_SKIN, !opts.disableSkinQuantize);
    ds->autoIncrement = opts.autoIncrementFilename;
    ds->openFolder = opts.openFolderAfterExport;

    setCheck(hDlg, IDC_CHK_TEX_CONVERT,   opts.texConvertEnabled);
    setCheck(hDlg, IDC_CHK_TEX_MIPMAPS,   opts.texGenerateMipmaps);
    setCheck(hDlg, IDC_CHK_TEX_OVERWRITE, opts.texOverwriteExisting);

    SendDlgItemMessageW(hDlg, IDC_CMB_BLP_COMPRESSION, CB_SETCURSEL, opts.blpCompression, 0);
    {
        wchar_t jpegText[16];
        _itow_s(opts.blpJpegQuality, jpegText, 10);
        SetDlgItemTextW(hDlg, IDC_EDT_BLP_JPEG_QUALITY, jpegText);
    }
    setCheck(hDlg, IDC_CHK_BLP_DITHERING, opts.blpDithering);
    SendDlgItemMessageW(hDlg, IDC_CMB_DDS_FORMAT, CB_SETCURSEL, opts.ddsFormat, 0);

    wchar_t precText[16];
    _itow_s(opts.extentsPrecision, precText, 10);
    SetDlgItemTextW(hDlg, IDC_EDT_EXTENTS_PREC, precText);
}

// ── Localization ───────────────────────────────────────────
//
// The .rc gives every control an English caption; this replaces them from the
// shared catalog (src/pre_startup_scripts/WhiteoutDexLocalization.ms) in one
// MaxScript round trip. Owner-drawn buttons paint their window text, so they
// are relabelled here as well. A key the catalog does not carry leaves its
// control English.

constexpr wdx::l10n::DialogString kExportStrings[] = {
    {0, "exp_export_settings_title"},

    {IDC_LBL_MODEL_NAME, "exp_model_name_grp"},
    {IDC_LBL_FORMAT, "exp_format_lbl"},
    {IDC_BTN_FORMAT_CLASSIC, "exp_format_classic_title"},
    {IDC_BTN_FORMAT_REFORGED, "exp_format_reforged_title"},
    {IDC_BTN_TAB_GEOMETRY, "exp_geometry_tab"},
    {IDC_BTN_TAB_TEXTURES, "exp_textures_tab"},

    // Geometry tab
    {IDC_CHK_MERGE_SIMILAR, "exp_merge_similar_meshes_chk"},
    {IDC_CHK_FIX_SHARED_NORMALS, "exp_fix_shared_normals_chk"},
    {IDC_CHK_KEEP_UNUSED_BH, "exp_keep_unused_boneshelpers_chk"},
    {IDC_CHK_QUANTIZE_SKIN, "exp_quantize_skin_weights_chk"},
    {IDC_LBL_EXTENTS_PREC, "exp_extents_precision_lbl"},

    // Textures tab
    {IDC_CHK_TEX_CONVERT, "exp_convert_textures_chk"},
    {IDC_LBL_BLP_COMPRESSION, "exp_compression_lbl"},
    {IDC_LBL_BLP_JPEG_QUALITY, "exp_jpeg_quality_lbl"},
    {IDC_CHK_BLP_DITHERING, "exp_dithering_chk"},
    {IDC_LBL_DDS_FORMAT, "exp_dds_format_lbl"},
    {IDC_CHK_TEX_MIPMAPS, "exp_regenerate_mipmaps_chk"},
    {IDC_CHK_TEX_OVERWRITE, "exp_overwrite_existing_chk"},

    // Banner, footer
    {IDC_BTN_SCENE_FIX_ALL, "exp_fix_all_btn"},
    {IDC_BTN_SCENE_DETAILS, "exp_details_btn"},
    {IDC_CHK_AUTO_INCREMENT, "exp_autoincrement_filename_chk"},
    {IDC_CHK_OPEN_FOLDER, "exp_open_folder_after_export_chk"},
    {IDOK, "exp_export_btn"},
    {IDCANCEL, "common_cancel_btn"},
};

// Text the dialog paints itself (no window to relabel).
struct DrawnString {
    const char* key;
    const wchar_t* fallback;
    std::wstring DialogState::*field;
    int intArgs;  // required number of "%d"; anything else falls back to English
};

const DrawnString kDrawnStrings[] = {
    {"exp_format_classic_sub", L"v800, BLP", &DialogState::subClassic, 0},
    {"exp_format_reforged_sub", L"v1800, DDS", &DialogState::subReforged, 0},
    {"exp_target_new", L"New file", &DialogState::targetNew, 0},
    {"exp_target_exists_increment_fmt", L"File exists. The new one gets _%d.", &DialogState::targetIncrementFmt, 1},
    {"exp_target_exists_overwrite", L"File exists and will be overwritten.", &DialogState::targetOverwrite, 0},
    {"exp_target_pending_title", L"No file chosen yet", &DialogState::targetPendingTitle, 0},
    {"exp_target_pending_note", L"The file is chosen after this dialog.", &DialogState::targetPendingNote, 0},
    {"exp_banner_one_problem_fmt", L"%d problem", &DialogState::bannerOneFmt, 1},
    {"exp_banner_n_problems_fmt", L"%d problems", &DialogState::bannerManyFmt, 1},
    {"exp_banner_summary_fmt", L"%d names, %d meshes, %d controllers, %d materials", &DialogState::bannerSummaryFmt, 4},
    {"exp_banner_no_problems", L"No problems", &DialogState::bannerNone, 0},
    {"exp_banner_checked", L"Checked: names, meshes, controllers, materials", &DialogState::bannerChecked, 0},
};

void loadDrawnStrings(DialogState* ds) {
    std::vector<const char*> keys;
    for (const DrawnString& d : kDrawnStrings)
        keys.push_back(d.key);
    const std::vector<std::wstring> values = wdx::l10n::TranslateMany(keys);

    size_t i = 0;
    for (const DrawnString& d : kDrawnStrings) {
        std::wstring v = (i < values.size()) ? values[i] : std::wstring();
        ++i;
        // WdxL.t returns the key itself for a missing entry.
        bool usable = !v.empty() && v != wdx::l10n::detail::Utf8ToWide(d.key);
        if (usable && countToken(v, L"%d") != d.intArgs)
            usable = false;
        ds->*(d.field) = usable ? v : std::wstring(d.fallback);
    }
}

void initBlpCompressionCombo(HWND hDlg) {
    HWND hCmb = GetDlgItem(hDlg, IDC_CMB_BLP_COMPRESSION);
    SendMessageW(hCmb, CB_RESETCONTENT, 0, 0);
    // "JPEG" is a format name and stays; only the paletted entry has prose.
    const std::wstring paletted = wdx::l10n::TrOr("exp_blp_paletted_item", L"Paletted (256 colors)");
    SendMessageW(hCmb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(paletted.c_str()));
    SendMessageW(hCmb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"JPEG"));
    SendMessageW(hCmb, CB_SETCURSEL, 0, 0);
}

void initDdsFormatCombo(HWND hDlg) {
    HWND hCmb = GetDlgItem(hDlg, IDC_CMB_DDS_FORMAT);
    SendMessageW(hCmb, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCmb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"BC3 (DXT5)"));
    SendMessageW(hCmb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"BC7"));
    SendMessageW(hCmb, CB_SETCURSEL, 0, 0);
}

// ============================================================================
// State -> UI
// ============================================================================

void updateHeaderText(DialogState* ds) {
    ds->headerWarn = false;
    if (ds->targetPath.empty()) {
        ds->headerLine1 = ds->targetPendingTitle;
        ds->headerLine2 = ds->targetPendingNote;
        return;
    }
    if (!mdx_export::FileExistsW(ds->targetPath)) {
        ds->headerLine1 = fileNameOf(ds->targetPath);
        ds->headerLine2 = ds->targetNew;
        return;
    }
    if (ds->autoIncrement) {
        int n = 1;
        ds->headerLine1 = fileNameOf(mdx_export::AutoIncrementPath(ds->targetPath, &n));
        ds->headerLine2 = fillInts(ds->targetIncrementFmt, {n});
        return;
    }
    ds->headerLine1 = fileNameOf(ds->targetPath);
    ds->headerLine2 = ds->targetOverwrite;
    ds->headerWarn = true;
}

void invalidateHeader(HWND hDlg, const DialogState* ds) {
    RECT r{0, 0, ds->S(lay::clientW), ds->S(lay::headerH)};
    InvalidateRect(hDlg, &r, FALSE);
}

void invalidateTabStrip(HWND hDlg, const DialogState* ds) {
    RECT r{0, ds->S(lay::underlineY), ds->S(lay::clientW), ds->S(lay::underlineY + lay::underlineH)};
    InvalidateRect(hDlg, &r, FALSE);
}

void invalidateBanner(HWND hDlg, const DialogState* ds) {
    RECT r = ds->R(lay::banner);
    InvalidateRect(hDlg, &r, FALSE);
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
    setTabStop(hDlg, IDC_BTN_FORMAT_CLASSIC, !ds->reforged);
    setTabStop(hDlg, IDC_BTN_FORMAT_REFORGED, ds->reforged);
    setTabStop(hDlg, IDC_BTN_TAB_GEOMETRY, ds->tab == 0);
    setTabStop(hDlg, IDC_BTN_TAB_TEXTURES, ds->tab == 1);
}

void applyTextureEnabledState(HWND hDlg) {
    const bool on = getCheck(hDlg, IDC_CHK_TEX_CONVERT);
    for (int id : kTextureSubControls)
        enableCtrl(hDlg, id, on);
}

// Page visibility plus the three Textures layouts (spec 3.7).
void applyPageLayout(HWND hDlg, DialogState* ds) {
    const bool geo = ds->tab == 0;
    const bool tex = !geo;
    const bool classic = !ds->reforged;
    const bool jpeg = comboSel(hDlg, IDC_CMB_BLP_COMPRESSION) == 1;

    // Move before showing, so a control never paints at its old position.
    const int mipY = !classic ? lay::mipYReforged : (jpeg ? lay::mipYJpeg : lay::mipYPaletted);
    place(hDlg, ds, IDC_CHK_TEX_MIPMAPS, Px{lay::subX, mipY, lay::subW, lay::rowH});
    place(hDlg, ds, IDC_CHK_TEX_OVERWRITE, Px{lay::subX, mipY + lay::rowPitch, lay::subW, lay::rowH});

    for (int id : kGeometryControls)
        showCtrl(hDlg, id, geo);
    // The exporter only quantizes skin weights for v800 (mdx_model_builder.cpp,
    // `opts.version < 1200`); for Reforged the option does nothing. The check
    // state stays, so switching back to Classic keeps the user's choice.
    enableCtrl(hDlg, IDC_CHK_QUANTIZE_SKIN, classic);

    showCtrl(hDlg, IDC_CHK_TEX_CONVERT, tex);
    showCtrl(hDlg, IDC_LBL_BLP_COMPRESSION, tex && classic);
    showCtrl(hDlg, IDC_CMB_BLP_COMPRESSION, tex && classic);
    showCtrl(hDlg, IDC_LBL_BLP_JPEG_QUALITY, tex && classic && jpeg);
    showCtrl(hDlg, IDC_EDT_BLP_JPEG_QUALITY, tex && classic && jpeg);
    showCtrl(hDlg, IDC_SPIN_BLP_JPEG_QUALITY, tex && classic && jpeg);
    showCtrl(hDlg, IDC_CHK_BLP_DITHERING, tex && classic && !jpeg);
    showCtrl(hDlg, IDC_LBL_DDS_FORMAT, tex && !classic);
    showCtrl(hDlg, IDC_CMB_DDS_FORMAT, tex && !classic);
    showCtrl(hDlg, IDC_CHK_TEX_MIPMAPS, tex);
    showCtrl(hDlg, IDC_CHK_TEX_OVERWRITE, tex);

    // Repaint the whole page area, dialog background and every child in it,
    // so nothing of the previous page or layout survives.
    RECT page{0, ds->S(lay::underlineY + lay::underlineH), ds->S(lay::clientW), ds->S(lay::banner.y)};
    RedrawWindow(hDlg, &page, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void selectFormat(HWND hDlg, DialogState* ds, bool reforged) {
    if (ds->reforged == reforged) return;
    ds->reforged = reforged;
    updateTabStops(hDlg, ds);
    InvalidateRect(GetDlgItem(hDlg, IDC_BTN_FORMAT_CLASSIC), nullptr, FALSE);
    InvalidateRect(GetDlgItem(hDlg, IDC_BTN_FORMAT_REFORGED), nullptr, FALSE);
    applyPageLayout(hDlg, ds);
}

void selectTab(HWND hDlg, DialogState* ds, int tab) {
    if (ds->tab == tab) return;
    ds->tab = tab;
    updateTabStops(hDlg, ds);
    InvalidateRect(GetDlgItem(hDlg, IDC_BTN_TAB_GEOMETRY), nullptr, FALSE);
    InvalidateRect(GetDlgItem(hDlg, IDC_BTN_TAB_TEXTURES), nullptr, FALSE);
    invalidateTabStrip(hDlg, ds);
    applyPageLayout(hDlg, ds);
}

// Run a scan and refresh the banner. Called on dialog open, after Fix All and
// after the Problem Details dialog closes.
void updateSceneStatus(HWND hDlg, DialogState* ds) {
    using scene_monitor::ProblemType;
    const scene_monitor::ScanResult result = scene_monitor::scanScene();
    ds->total       = result.count();
    ds->names       = result.countByType(ProblemType::DuplicateName);
    ds->meshes      = result.countByType(ProblemType::EmptyMesh)
                    + result.countByType(ProblemType::EditablePoly)
                    + result.countByType(ProblemType::EditMeshAboveSkin)
                    + result.countByType(ProblemType::MultiMaterialMesh);
    ds->controllers = result.countByType(ProblemType::InvalidController);
    ds->materials   = result.countByType(ProblemType::UnsupportedMaterial)
                    + result.countByType(ProblemType::DuplicateMaterial);

    const bool problems = ds->total > 0;
    HWND focus = GetFocus();
    const bool focusInBanner = focus == GetDlgItem(hDlg, IDC_BTN_SCENE_FIX_ALL)
                            || focus == GetDlgItem(hDlg, IDC_BTN_SCENE_DETAILS);
    showCtrl(hDlg, IDC_BTN_SCENE_FIX_ALL, problems);
    showCtrl(hDlg, IDC_BTN_SCENE_DETAILS, problems);
    if (!problems && focusInBanner)
        SendMessageW(hDlg, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(GetDlgItem(hDlg, IDOK)), TRUE);
    invalidateBanner(hDlg, ds);
}

// ============================================================================
// Layout
// ============================================================================

void layoutExportDialog(HWND hDlg, DialogState* ds) {
    RECT rc{0, 0, ds->S(lay::clientW), ds->S(lay::clientH)};
    AdjustWindowRectEx(&rc, static_cast<DWORD>(GetWindowLongPtrW(hDlg, GWL_STYLE)), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(hDlg, GWL_EXSTYLE)));
    SetWindowPos(hDlg, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    place(hDlg, ds, IDC_LBL_MODEL_NAME, lay::lblModel);
    place(hDlg, ds, IDC_EDT_MODEL_NAME, lay::edtModel);
    place(hDlg, ds, IDC_LBL_FORMAT, lay::lblFormat);
    place(hDlg, ds, IDC_BTN_FORMAT_CLASSIC, lay::cardClassic);
    place(hDlg, ds, IDC_BTN_FORMAT_REFORGED, lay::cardReforged);

    // Tabs are as wide as their text, 22 px apart.
    const int tabIds[2] = {IDC_BTN_TAB_GEOMETRY, IDC_BTN_TAB_TEXTURES};
    int x = ds->S(lay::tabX);
    for (int i = 0; i < 2; ++i) {
        const int w = textWidth(hDlg, ds->font, windowText(GetDlgItem(hDlg, tabIds[i]))) + ds->S(2);
        placeDevice(hDlg, tabIds[i], x, ds->S(lay::tabY), w, ds->S(lay::tabH));
        ds->tabRect[i] = RECT{x, ds->S(lay::tabY), x + w, ds->S(lay::tabY + lay::tabH)};
        x += w + ds->S(lay::tabGap);
    }

    // Geometry tab
    place(hDlg, ds, IDC_CHK_MERGE_SIMILAR, lay::chkMerge);
    place(hDlg, ds, IDC_CHK_FIX_SHARED_NORMALS, lay::chkNormals);
    place(hDlg, ds, IDC_CHK_KEEP_UNUSED_BH, lay::chkKeep);
    place(hDlg, ds, IDC_CHK_QUANTIZE_SKIN, lay::chkQuantize);
    // Label columns keep their design width but grow for long translations
    // (French "Précision des étendues" needs more than 120 px); the fields
    // move right with them.
    auto labelWidth = [&](int baseW, std::initializer_list<int> ids) {
        int w = ds->S(baseW);
        for (int id : ids)
            w = std::max(w, textWidth(hDlg, ds->font, windowText(GetDlgItem(hDlg, id))) + ds->S(2));
        return w;
    };
    const int labelGap = ds->S(lay::edtPrecision.x - lay::lblPrecision.x - lay::lblPrecision.w);  // 7
    const int precLabelW = labelWidth(lay::lblPrecision.w, {IDC_LBL_EXTENTS_PREC});
    const int precEditX = ds->S(lay::lblPrecision.x) + precLabelW + labelGap;
    placeDevice(hDlg, IDC_LBL_EXTENTS_PREC, ds->S(lay::lblPrecision.x), ds->S(lay::lblPrecision.y), precLabelW,
                ds->S(lay::lblPrecision.h));
    placeDevice(hDlg, IDC_EDT_EXTENTS_PREC, precEditX, ds->S(lay::edtPrecision.y), ds->S(lay::edtPrecision.w),
                ds->S(lay::edtPrecision.h));
    placeDevice(hDlg, IDC_SPIN_EXTENTS_PREC, precEditX + ds->S(lay::edtPrecision.w), ds->S(lay::spnPrecision.y),
                ds->S(lay::spnPrecision.w), ds->S(lay::spnPrecision.h));

    // Textures tab (Mipmaps / Overwrite are placed by applyPageLayout)
    place(hDlg, ds, IDC_CHK_TEX_CONVERT, lay::chkConvert);
    const int texLabelW = labelWidth(lay::lblTexRow1.w,
                                     {IDC_LBL_BLP_COMPRESSION, IDC_LBL_BLP_JPEG_QUALITY, IDC_LBL_DDS_FORMAT});
    const int fieldX = ds->S(lay::lblTexRow1.x) + texLabelW + labelGap;
    const int fieldRight = ds->S(lay::cmbTexRow1.x + lay::cmbTexRow1.w);
    const int comboW = std::max(ds->S(80), fieldRight - fieldX);
    placeDevice(hDlg, IDC_LBL_BLP_COMPRESSION, ds->S(lay::lblTexRow1.x), ds->S(lay::lblTexRow1.y), texLabelW,
                ds->S(lay::lblTexRow1.h));
    placeCombo(hDlg, ds, IDC_CMB_BLP_COMPRESSION, fieldX, ds->S(lay::cmbTexRow1.y), comboW, ds->S(lay::cmbTexRow1.h));
    placeDevice(hDlg, IDC_LBL_DDS_FORMAT, ds->S(lay::lblTexRow1.x), ds->S(lay::lblTexRow1.y), texLabelW,
                ds->S(lay::lblTexRow1.h));
    placeCombo(hDlg, ds, IDC_CMB_DDS_FORMAT, fieldX, ds->S(lay::cmbTexRow1.y), comboW, ds->S(lay::cmbTexRow1.h));
    place(hDlg, ds, IDC_CHK_BLP_DITHERING, lay::chkDither);
    placeDevice(hDlg, IDC_LBL_BLP_JPEG_QUALITY, ds->S(lay::lblJpeg.x), ds->S(lay::lblJpeg.y), texLabelW,
                ds->S(lay::lblJpeg.h));
    placeDevice(hDlg, IDC_EDT_BLP_JPEG_QUALITY, fieldX, ds->S(lay::edtJpeg.y), ds->S(lay::edtJpeg.w),
                ds->S(lay::edtJpeg.h));
    placeDevice(hDlg, IDC_SPIN_BLP_JPEG_QUALITY, fieldX + ds->S(lay::edtJpeg.w), ds->S(lay::spnJpeg.y),
                ds->S(lay::spnJpeg.w), ds->S(lay::spnJpeg.h));

    // Banner buttons, right-aligned inside the banner
    const int pad = ds->S(lay::bannerBtnPad) * 2;
    const int detailsW = textWidth(hDlg, ds->font, windowText(GetDlgItem(hDlg, IDC_BTN_SCENE_DETAILS))) + pad;
    const int fixW = textWidth(hDlg, ds->font, windowText(GetDlgItem(hDlg, IDC_BTN_SCENE_FIX_ALL))) + pad;
    const int detailsX = ds->S(lay::bannerBtnRight) - detailsW;
    const int fixX = detailsX - ds->S(lay::bannerBtnGap) - fixW;
    placeDevice(hDlg, IDC_BTN_SCENE_DETAILS, detailsX, ds->S(lay::bannerBtnY), detailsW, ds->S(lay::bannerBtnH));
    placeDevice(hDlg, IDC_BTN_SCENE_FIX_ALL, fixX, ds->S(lay::bannerBtnY), fixW, ds->S(lay::bannerBtnH));
    ds->fixAllLeft = fixX;

    // Footer buttons from the right edge, check buttons fill the rest
    const int btnPad = ds->S(lay::footerBtnPad) * 2;
    const int cancelW = std::max(ds->S(lay::footerBtnMinW),
                                 textWidth(hDlg, ds->font, windowText(GetDlgItem(hDlg, IDCANCEL))) + btnPad);
    const int exportW = std::max(ds->S(lay::footerBtnMinW),
                                 textWidth(hDlg, ds->font, windowText(GetDlgItem(hDlg, IDOK))) + btnPad);
    const int cancelX = ds->S(lay::footerRight) - cancelW;
    const int exportX = cancelX - ds->S(lay::footerBtnGap) - exportW;
    placeDevice(hDlg, IDCANCEL, cancelX, ds->S(lay::footerBtnY), cancelW, ds->S(lay::footerBtnH));
    placeDevice(hDlg, IDOK, exportX, ds->S(lay::footerBtnY), exportW, ds->S(lay::footerBtnH));

    const int chkX = ds->S(lay::footerChkX);
    const int chkW = std::max(ds->S(40), exportX - ds->S(11) - chkX);
    placeDevice(hDlg, IDC_CHK_AUTO_INCREMENT, chkX, ds->S(lay::footerChk1Y), chkW, ds->S(lay::rowH));
    placeDevice(hDlg, IDC_CHK_OPEN_FOLDER, chkX, ds->S(lay::footerChk2Y), chkW, ds->S(lay::rowH));
}

// ============================================================================
// Painting
// ============================================================================

// Footer while the export runs: step text and percentage on one row, the bar
// below, across the width of the content.
void paintProgress(HWND hDlg, HDC dc, const DialogState* ds) {
    const int x0 = ds->S(lay::footerChkX), x1 = ds->S(lay::footerRight);
    const int textTop = ds->S(lay::progTextY), textBottom = ds->S(lay::progTextY + lay::progTextH);

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

    const RECT track{x0, ds->S(lay::progBarY), x1, ds->S(lay::progBarY + lay::progBarH)};
    const float radius = (track.bottom - track.top) * 0.5f;
    fillRoundRect(dc, track, radius, col::tabLine);
    // At least as wide as high, so the rounded ends never overlap.
    const LONG fillW = static_cast<LONG>((track.right - track.left) * std::min(1.0f, std::max(0.0f, ds->progress)));
    if (fillW > 0)
        fillRoundRect(dc, RECT{track.left, track.top, track.left + std::max(fillW, track.bottom - track.top),
                               track.bottom},
                      radius, col::accent);
}

// Everything the dialog paints itself, drawn into |target| (client
// coordinates). Double-buffered: the GDI+ shapes then always start from a
// memory DC at the origin, whatever viewport offset |target| carries.
void drawDialogInto(HWND hDlg, const DialogState* ds, HDC target) {
    RECT client{};
    GetClientRect(hDlg, &client);
    {
        BufferedPaint buffer(target, client);
        HDC dc = buffer.dc();

        fillRectColor(dc, client, col::body);

        // Header band
        fillRectColor(dc, RECT{0, 0, client.right, ds->S(lay::headerH)}, col::band);
        fillRectColor(dc, RECT{0, ds->S(lay::headerLineY), client.right,
                               ds->S(lay::headerLineY) + std::max(1, ds->S(1))}, col::bandLine);
        drawDocIcon(dc, ds->R(lay::docIcon));
        drawTextIn(dc, ds->headerLine1, ds->R(lay::fileName), ds->fontSemibold, col::textBright, kLine);
        drawTextIn(dc, ds->headerLine2, ds->R(lay::fileNote), ds->font,
                   ds->headerWarn ? col::textWarn : col::textDim, kLine);

        // Tab strip line and the active tab's underline
        const RECT strip = ds->R(lay::tabStrip);
        fillRectColor(dc, RECT{strip.left, strip.top, strip.right, strip.top + std::max(1, ds->S(1))}, col::tabLine);
        RECT underline = ds->tabRect[ds->tab];
        underline.top = ds->S(lay::underlineY);
        underline.bottom = underline.top + ds->S(lay::underlineH);
        fillRectColor(dc, underline, col::accent);

        // Scene check banner
        const bool problems = ds->total > 0;
        const RECT banner = ds->R(lay::banner);
        borderedRoundRect(dc, banner, ds->Sf(4.0f), std::max(1, ds->S(1)),
                          problems ? col::warnBorder : col::okBorder, problems ? col::warnBg : col::okBg);
        if (problems)
            drawWarnIcon(dc, ds->R(lay::bannerIcon));
        else
            drawOkIcon(dc, ds->R(lay::bannerIcon));

        const std::wstring title = problems
            ? fillInts(ds->total == 1 ? ds->bannerOneFmt : ds->bannerManyFmt, {ds->total})
            : ds->bannerNone;
        const std::wstring summary = problems
            ? fillInts(ds->bannerSummaryFmt, {ds->names, ds->meshes, ds->controllers, ds->materials})
            : ds->bannerChecked;
        const int titleRight = problems ? ds->fixAllLeft - ds->S(8) : banner.right - ds->S(9);
        drawTextIn(dc, title,
                   RECT{ds->S(lay::bannerTextX), ds->S(lay::bannerTitleY), titleRight,
                        ds->S(lay::bannerTitleY + lay::bannerTextH)},
                   ds->fontSemibold, col::bannerTitle, kLine);
        drawTextIn(dc, summary, ds->R(lay::bannerSub), ds->font, col::bannerSub, kLine);

        // Footer band (the progress once the export runs)
        fillRectColor(dc, RECT{0, ds->S(lay::footerY), client.right, client.bottom}, col::band);
        fillRectColor(dc, RECT{0, ds->S(lay::footerY), client.right,
                               ds->S(lay::footerY) + std::max(1, ds->S(1))}, col::bandLine);
        if (ds->busy)
            paintProgress(hDlg, dc, ds);
    }
}

void paintDialog(HWND hDlg, const DialogState* ds) {
    PAINTSTRUCT ps;
    HDC target = BeginPaint(hDlg, &ps);
    drawDialogInto(hDlg, ds, target);
    EndPaint(hDlg, &ps);
}

// ============================================================================
// Progress (after Export)
// ============================================================================

// Leaves the options: the footer's check buttons and buttons disappear, every
// control is disabled, and the footer shows the progress from now on.
void beginProgress(HWND hDlg, DialogState* ds) {
    std::vector<const char*> keys;
    for (const StepInfo& s : kSteps) keys.push_back(s.key);
    const std::vector<std::wstring> values = wdx::l10n::TranslateMany(keys);
    ds->stepText.clear();
    for (size_t i = 0; i < std::size(kSteps); ++i) {
        const std::wstring v = i < values.size() ? values[i] : std::wstring();
        // WdxL.t returns the key itself for a missing entry.
        const bool usable = !v.empty() && v != wdx::l10n::detail::Utf8ToWide(kSteps[i].key);
        ds->stepText.push_back(usable ? v : std::wstring(kSteps[i].english));
    }

    ds->busy = true;
    for (int id : {IDOK, IDCANCEL, IDC_CHK_AUTO_INCREMENT, IDC_CHK_OPEN_FOLDER})
        ShowWindow(GetDlgItem(hDlg, id), SW_HIDE);
    for (HWND c = GetWindow(hDlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        EnableWindow(c, FALSE);
    InvalidateRect(hDlg, nullptr, FALSE);
    UpdateWindow(hDlg);
}

// Repaints the footer (at most every 50 ms unless |force|) and keeps the
// window responsive: the export runs on this thread and dispatches nothing,
// and Windows treats a thread that has not called PeekMessage for 5 seconds as
// not responding (IsHungAppWindow). PM_NOREMOVE only looks: Max is disabled,
// so what the user types or clicks meanwhile is aimed at this window, which
// no longer exists when Max reads its queue again.
void refreshProgress(HWND hDlg, DialogState* ds, bool force) {
    const ULONGLONG now = GetTickCount64();
    if (force || now - ds->lastPaint >= 50) {
        ds->lastPaint = now;
        RECT r{0, ds->S(lay::footerY), ds->S(lay::clientW), ds->S(lay::clientH)};
        InvalidateRect(hDlg, &r, FALSE);
        UpdateWindow(hDlg);
    }
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);
}

bool focusVisible(const DRAWITEMSTRUCT* dis) {
    return (dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT);
}

void drawCard(const DRAWITEMSTRUCT* dis, const DialogState* ds, bool reforgedCard) {
    BufferedPaint buffer(dis->hDC, dis->rcItem);
    HDC dc = buffer.dc();
    const RECT rc = buffer.local();
    const bool selected = ds->reforged == reforgedCard;
    const bool hover = isHover(dis->hwndItem);

    fillRectColor(dc, rc, col::body);
    const COLORREF fill = selected ? col::cardSelBg : col::cardBg;
    // 1 px border; the selected card adds a 1 px accent ring inside it (2 px in total).
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
    drawTextIn(dc, reforgedCard ? ds->subReforged : ds->subClassic,
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

void drawBannerButton(const DRAWITEMSTRUCT* dis, const DialogState* ds) {
    BufferedPaint buffer(dis->hDC, dis->rcItem);
    HDC dc = buffer.dc();
    const RECT rc = buffer.local();
    const bool active = isHover(dis->hwndItem) || (dis->itemState & ODS_SELECTED);

    fillRectColor(dc, rc, col::warnBg);
    fillRoundRect(dc, rc, ds->Sf(3.0f), active ? col::smallBtnHov : col::smallBtn);
    drawTextIn(dc, windowText(dis->hwndItem), rc, ds->font, col::btnText,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (focusVisible(dis))
        frameRectColor(dc, inset(rc, ds->S(3)), col::focus);
}

void drawFooterCheck(const DRAWITEMSTRUCT* dis, const DialogState* ds, bool checked) {
    BufferedPaint buffer(dis->hDC, dis->rcItem);
    HDC dc = buffer.dc();
    const RECT rc = buffer.local();

    fillRectColor(dc, rc, col::band);
    const int box = ds->S(13);
    const int top = (rc.bottom - box) / 2;
    const RECT boxRect{0, top, box, top + box};
    fillRoundRect(dc, boxRect, ds->Sf(2.0f), isHover(dis->hwndItem) ? col::checkBoxHov : col::checkBox);
    if (checked)
        strokePolyline(dc, 0.0f, static_cast<float>(top), static_cast<float>(box), 13.0f,
                       {{3.2f, 6.6f}, {5.6f, 9.0f}, {10.0f, 4.4f}}, 2.0f, col::checkMark);

    const std::wstring label = windowText(dis->hwndItem);
    RECT textRect{ds->S(19), 0, rc.right, rc.bottom};
    drawTextIn(dc, label, textRect, ds->font, col::text, kLine);

    if (focusVisible(dis)) {
        RECT measure = textRect;
        HGDIOBJ old = SelectObject(dc, ds->font);
        DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &measure,
                  DT_LEFT | DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        SelectObject(dc, old);
        const int textH = measure.bottom - measure.top;
        RECT f{textRect.left - 1, (rc.bottom - textH) / 2 - 1,
               std::min(static_cast<int>(rc.right), static_cast<int>(measure.right) + 1),
               (rc.bottom + textH) / 2 + 1};
        frameRectColor(dc, f, col::focus);
    }
}

void drawExportButton(const DRAWITEMSTRUCT* dis, const DialogState* ds) {
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

void createResources(DialogState* ds) {
    const int height = -MulDiv(9, ds->dpi, 72);  // Segoe UI 9 pt
    ds->font = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    ds->fontSemibold = CreateFontW(height, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    ds->brBody = CreateSolidBrush(col::body);
    ds->brBand = CreateSolidBrush(col::band);
    ds->brEdit = CreateSolidBrush(col::editBg);
    ds->brWarn = CreateSolidBrush(col::warnBg);
}

void destroyResources(DialogState* ds) {
    for (HFONT* f : {&ds->font, &ds->fontSemibold})
        if (*f) { DeleteObject(*f); *f = nullptr; }
    for (HBRUSH* b : {&ds->brBody, &ds->brBand, &ds->brEdit, &ds->brWarn})
        if (*b) { DeleteObject(*b); *b = nullptr; }
}

bool isEditControl(HWND h) {
    wchar_t cls[16];
    return GetClassNameW(h, cls, 16) > 0 && lstrcmpiW(cls, L"Edit") == 0;
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
    wdx::l10n::LocalizeDialog(hDlg, kExportStrings);
    loadDrawnStrings(ds);

    for (int id : kAllControls)
        SendDlgItemMessageW(hDlg, id, WM_SETFONT, reinterpret_cast<WPARAM>(ds->font), FALSE);

    SendDlgItemMessageW(hDlg, IDC_SPIN_EXTENTS_PREC, UDM_SETRANGE32, 0, 10);
    SendDlgItemMessageW(hDlg, IDC_SPIN_BLP_JPEG_QUALITY, UDM_SETRANGE32, 1, 100);
    initBlpCompressionCombo(hDlg);
    initDdsFormatCombo(hDlg);

    // Populate controls from defaults, then overlay with INI
    optionsToDialog(hDlg, ds, *ds->opts);
    loadDialogSettingsFromINI(hDlg, ds);

    // Model Name follows the scene: a saved scene always seeds the field
    // with its own name, overriding whatever the INI restored a moment ago.
    // The persisted value is per-user, not per-scene, so leaving it in
    // place stamped the previously exported model's name onto every later
    // export. The user can still type over it for this one export.
    // An unsaved scene keeps the INI value (there is nothing better).
    if (std::wstring sceneName = currentSceneModelName(); !sceneName.empty())
        SetDlgItemTextW(hDlg, IDC_EDT_MODEL_NAME, sceneName.c_str());

    // Dithering only applies to paletted BLP (same rule as before).
    if (comboSel(hDlg, IDC_CMB_BLP_COMPRESSION) == 1)
        setCheck(hDlg, IDC_CHK_BLP_DITHERING, false);

    for (int id : kOwnerDrawn)
        installHover(hDlg, id);

    updateHeaderText(ds);
    layoutExportDialog(hDlg, ds);
    updateTabStops(hDlg, ds);
    applyPageLayout(hDlg, ds);
    applyTextureEnabledState(hDlg);

    // Initial scene scan for the banner. Cheap enough on open — one scan.
    updateSceneStatus(hDlg, ds);

    // Center dialog on parent
    CenterWindow(hDlg, GetParent(hDlg));
    return TRUE;
}

INT_PTR CALLBACK ExportDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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
        // Leaving that DC untouched kept the old pixels under the text, so a
        // moved or re-shown checkbox showed a ghost of the other tab.
        HDC dc = reinterpret_cast<HDC>(wParam);
        if (dc && WindowFromDC(dc) != hDlg)
            drawDialogInto(hDlg, ds, dc);
        SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, TRUE);
        return TRUE;
    }

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
        const bool enabled = IsWindowEnabled(ctl) != FALSE;
        if (isEditControl(ctl)) {  // disabled / read-only edits land here
            SetTextColor(dc, enabled ? col::editText : col::textDisabled);
            SetBkColor(dc, col::editBg);
            return reinterpret_cast<INT_PTR>(ds->brEdit);
        }
        const int id = GetDlgCtrlID(ctl);
        COLORREF c = (id == IDC_LBL_MODEL_NAME || id == IDC_LBL_FORMAT) ? col::textDim : col::text;
        if (!enabled) c = col::textDisabled;
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
        if (id == IDCANCEL || id == IDOK || id == IDC_CHK_AUTO_INCREMENT || id == IDC_CHK_OPEN_FOLDER)
            return reinterpret_cast<INT_PTR>(ds->brBand);
        if (id == IDC_BTN_SCENE_FIX_ALL || id == IDC_BTN_SCENE_DETAILS)
            return reinterpret_cast<INT_PTR>(ds->brWarn);
        return reinterpret_cast<INT_PTR>(ds->brBody);
    }

    case WM_DRAWITEM: {
        if (!ds) break;
        const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (!dis || dis->CtlType != ODT_BUTTON) break;
        switch (dis->CtlID) {
        case IDC_BTN_FORMAT_CLASSIC:  drawCard(dis, ds, false); return TRUE;
        case IDC_BTN_FORMAT_REFORGED: drawCard(dis, ds, true); return TRUE;
        case IDC_BTN_TAB_GEOMETRY:    drawTab(dis, ds, 0); return TRUE;
        case IDC_BTN_TAB_TEXTURES:    drawTab(dis, ds, 1); return TRUE;
        case IDC_BTN_SCENE_FIX_ALL:
        case IDC_BTN_SCENE_DETAILS:   drawBannerButton(dis, ds); return TRUE;
        case IDC_CHK_AUTO_INCREMENT:  drawFooterCheck(dis, ds, ds->autoIncrement); return TRUE;
        case IDC_CHK_OPEN_FOLDER:     drawFooterCheck(dis, ds, ds->openFolder); return TRUE;
        case IDOK:                    drawExportButton(dis, ds); return TRUE;
        default: break;
        }
        break;
    }

    case WM_COMMAND: {
        if (!ds) break;
        int id   = LOWORD(wParam);
        int code = HIWORD(wParam);
        // Owner-drawn buttons report a quick second click as BN_DBLCLK.
        const bool clicked = code == BN_CLICKED || code == BN_DBLCLK;

        switch (id) {
        // Both end ExportDialog::run's loop; the window itself stays until
        // ExportDialog::close.
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

        // Cards and tabs select on click and when the arrow keys move focus
        // onto them (BN_SETFOCUS needs BS_NOTIFY, set in the .rc).
        case IDC_BTN_FORMAT_CLASSIC:
        case IDC_BTN_FORMAT_REFORGED:
            if (clicked || code == BN_SETFOCUS)
                selectFormat(hDlg, ds, id == IDC_BTN_FORMAT_REFORGED);
            return TRUE;

        case IDC_BTN_TAB_GEOMETRY:
        case IDC_BTN_TAB_TEXTURES:
            if (clicked || code == BN_SETFOCUS)
                selectTab(hDlg, ds, id == IDC_BTN_TAB_TEXTURES ? 1 : 0);
            return TRUE;

        case IDC_CHK_AUTO_INCREMENT:
            if (clicked) {
                ds->autoIncrement = !ds->autoIncrement;
                InvalidateRect(GetDlgItem(hDlg, id), nullptr, FALSE);
                updateHeaderText(ds);
                invalidateHeader(hDlg, ds);
            }
            return TRUE;

        case IDC_CHK_OPEN_FOLDER:
            if (clicked) {
                ds->openFolder = !ds->openFolder;
                InvalidateRect(GetDlgItem(hDlg, id), nullptr, FALSE);
            }
            return TRUE;

        case IDC_CHK_TEX_CONVERT:
            if (code == BN_CLICKED)
                applyTextureEnabledState(hDlg);
            return TRUE;

        case IDC_CMB_BLP_COMPRESSION:
            if (code == CBN_SELCHANGE) {
                // Force-uncheck so the saved opts don't carry a stale dithering
                // flag for a JPEG export.
                if (comboSel(hDlg, IDC_CMB_BLP_COMPRESSION) == 1)
                    setCheck(hDlg, IDC_CHK_BLP_DITHERING, false);
                applyPageLayout(hDlg, ds);
            }
            return TRUE;

        case IDC_BTN_SCENE_FIX_ALL:
            if (code == BN_CLICKED) {
                // Run all four fixers, then refresh the banner.
                auto result = scene_monitor::scanScene();
                scene_monitor::fixAll(result);
                updateSceneStatus(hDlg, ds);
            }
            return TRUE;

        case IDC_BTN_SCENE_DETAILS:
            if (code == BN_CLICKED) {
                // The Problem Details dialog re-scans internally on every fix
                // and on close; refresh the banner afterwards.
                scene_monitor::showProblemDetailsDialog(GetDllInstance(), hDlg);
                updateSceneStatus(hDlg, ds);
            }
            return TRUE;

        default:
            break;
        }
        break;
    }

    case WM_CLOSE:
        // No closing while the export runs; before that, closing is Cancel.
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

struct ExportDialogImpl {
    HINSTANCE hInstance = nullptr;
    HWND parent = nullptr;
    HWND hDlg = nullptr;
    bool parentDisabled = false;   // disabled by run(), enabled again by close()
    ULONG_PTR gdiplusToken = 0;
    DialogState ds;
};

ExportDialog::ExportDialog(HINSTANCE hInstance, HWND hWndParent)
    : impl_(std::make_unique<ExportDialogImpl>()) {
    impl_->hInstance = hInstance;
    impl_->parent = hWndParent;
}

ExportDialog::~ExportDialog() {
    close();
}

// A modal dialog built from a modeless one: DialogBox would destroy the window
// when the user clicks Export, and the window has to stay for the progress.
// Like DialogBox, this disables the owner while the window exists and runs
// its own message loop, handing a WM_QUIT back to the application's loop.
bool ExportDialog::run(MdxExportOptions& opts, const wchar_t* targetPath) {
    ExportDialogImpl& m = *impl_;
    if (m.hDlg) return false;

    m.ds = DialogState{};
    m.ds.opts = &opts;
    if (targetPath && targetPath[0])
        m.ds.targetPath = targetPath;

    // GDI+ draws the anti-aliased shapes; it must not be started in DllMain.
    Gdiplus::GdiplusStartupInput gdiplusInput;
    g_gdiplus = Gdiplus::GdiplusStartup(&m.gdiplusToken, &gdiplusInput, nullptr) == Gdiplus::Ok;

    m.hDlg = CreateDialogParamW(m.hInstance, MAKEINTRESOURCEW(IDD_EXPORT_OPTIONS), m.parent,
                                ExportDialogProc, reinterpret_cast<LPARAM>(&m.ds));
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

HWND ExportDialog::window() const {
    return impl_->hDlg;
}

void ExportDialog::step(ExportStep s) {
    ExportDialogImpl& m = *impl_;
    const size_t i = static_cast<size_t>(s);
    if (!m.hDlg || !m.ds.confirmed || i >= std::size(kSteps)) return;
    if (!m.ds.busy) beginProgress(m.hDlg, &m.ds);
    m.ds.step = static_cast<int>(i);
    m.ds.itemsDone = m.ds.itemsTotal = 0;
    m.ds.progress = kSteps[i].from;
    refreshProgress(m.hDlg, &m.ds, true);
}

void ExportDialog::items(size_t done, size_t total) {
    ExportDialogImpl& m = *impl_;
    if (!m.hDlg || !m.ds.busy || m.ds.step < 0 || total == 0) return;
    const StepInfo& s = kSteps[static_cast<size_t>(m.ds.step)];
    m.ds.itemsDone = done;
    m.ds.itemsTotal = total;
    m.ds.progress = s.from + (s.to - s.from) * static_cast<float>(std::min(done, total)) / static_cast<float>(total);
    refreshProgress(m.hDlg, &m.ds, false);
}

void ExportDialog::close() {
    ExportDialogImpl& m = *impl_;
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
