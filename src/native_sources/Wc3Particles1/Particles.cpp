/**
 * @file Particles.cpp
 * @brief Implementation of the Wc3Particles1 3ds Max particle plugin.
 *
 * Spherical/cone model-particle emitter based on CParticleEmitter pseudocode.
 * Emits from a point source with velocity cone defined by latitude/longitude.
 */

#include "Particles.h"

/// @name Module globals
/// @{
HINSTANCE hInstance = nullptr;
static MCHAR s_stringBuf[256];

static Wc3Particles1ClassDesc       p1Desc;
static Emitter1CreateCallback       emitter1Callback;
static Wc3Particles1ParticleDraw    theWc3Particles1Draw;
/// @}

// ============================================================================
// ParamBlock2 descriptor — 8 parameters
// ============================================================================
static ParamBlockDesc2 wc3particles1_param_blk(
    wc3particles1_params,
    _M("Wc3Particles1Parameters"),
    0,
    &p1Desc,
    P_AUTO_CONSTRUCT | P_AUTO_UI | P_MULTIMAP | P_HASCATEGORY,

    // --- P_AUTO_CONSTRUCT ref# ---
    0,

    // --- P_MULTIMAP: 4 rollouts ---
    P1_MAP_COUNT,
    P1_MAP_CONFIG,  IDD_P1_ROLLOUT_CONFIG,  IDS_P1_ROLLOUT_CONFIG,  0, ROLLUP_SAVECAT, NULL, ROLLUP_CAT_STANDARD,
    P1_MAP_EMITTER, IDD_P1_ROLLOUT_EMITTER, IDS_P1_ROLLOUT_EMITTER, 0, ROLLUP_SAVECAT, NULL, ROLLUP_CAT_STANDARD+1,
    P1_MAP_TIMING,  IDD_P1_ROLLOUT_TIMING,  IDS_P1_ROLLOUT_TIMING,  0, ROLLUP_SAVECAT, NULL, ROLLUP_CAT_STANDARD+2,
    P1_MAP_MODEL,   IDD_P1_ROLLOUT_MODEL,   IDS_P1_ROLLOUT_MODEL,   0, ROLLUP_SAVECAT, NULL, ROLLUP_CAT_STANDARD+3,

    // [0] P1_PB_COUNT — auto-computed max particles
    P1_PB_COUNT, _M("Count"), TYPE_INT, 0, IDS_P1_PARAM_COUNT,
        p_default,  500,
        p_range,    1, 100000000,
        p_ui,       P1_MAP_EMITTER, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_P1_EDIT_COUNT, IDC_P1_SPIN_COUNT, 1.0f,
    p_end,

    // [1] P1_PB_SPEED — initial velocity (matches CParticleEmitter::m_velocity)
    P1_PB_SPEED, _M("Speed"), TYPE_FLOAT, P_ANIMATABLE, IDS_P1_PARAM_SPEED,
        p_default,  10.0f,
        p_range,    -1000000000.0f, 1000000000.0f,
        p_ui,       P1_MAP_EMITTER, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_P1_EDIT_SPEED, IDC_P1_SPIN_SPEED, 0.005f,
    p_end,

    // [2] P1_PB_EMISSION_RATE — particles per second (matches m_particleEmissionRate)
    P1_PB_EMISSION_RATE, _M("EmissionRate"), TYPE_FLOAT, P_ANIMATABLE, IDS_P1_PARAM_EMISSION,
        p_default,  50.0f,
        p_range,    0.0f, 1000000000.0f,
        p_ui,       P1_MAP_TIMING, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_P1_EDIT_EMISSION, IDC_P1_SPIN_EMISSION, 0.1f,
    p_end,

    // [3] P1_PB_LIFE — lifespan in seconds (matches m_particleLifeSpan)
    P1_PB_LIFE, _M("Life"), TYPE_FLOAT, 0, IDS_P1_PARAM_LIFE,
        p_default,  1.0f,
        p_range,    0.0f, 1000000000.0f,
        p_ui,       P1_MAP_TIMING, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_P1_EDIT_LIFE, IDC_P1_SPIN_LIFE, 0.005f,
    p_end,

    // [4] P1_PB_ACCELERATION — gravity along -Z (matches m_acceleration)
    P1_PB_ACCELERATION, _M("Gravity"), TYPE_FLOAT, P_ANIMATABLE, IDS_P1_PARAM_ACCEL,
        p_default,  0.0f,
        p_range,    0.0f, 100.0f,
        p_ui,       P1_MAP_EMITTER, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_P1_EDIT_ACCEL, IDC_P1_SPIN_ACCEL, 0.1f,
    p_end,

    // [5] P1_PB_LATITUDE — emission cone half-angle in degrees (matches m_latitude)
    //     Default: 45° (pi/4 radians in the original engine)
    P1_PB_LATITUDE, _M("Latitude"), TYPE_FLOAT, P_ANIMATABLE, IDS_P1_PARAM_LATITUDE,
        p_default,  45.0f,
        p_range,    0.0f, 180.0f,
        p_ui,       P1_MAP_EMITTER, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_P1_EDIT_LATITUDE, IDC_P1_SPIN_LATITUDE, 0.5f,
    p_end,

    // [6] P1_PB_LONGITUDE — emission cone half-angle in degrees (matches m_longitude)
    //     Default: 45° (pi/4 radians in the original engine)
    P1_PB_LONGITUDE, _M("Longitude"), TYPE_FLOAT, P_ANIMATABLE, IDS_P1_PARAM_LONGITUDE,
        p_default,  45.0f,
        p_range,    0.0f, 180.0f,
        p_ui,       P1_MAP_EMITTER, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_P1_EDIT_LONGITUDE, IDC_P1_SPIN_LONGITUDE, 0.5f,
    p_end,

    // [7] P1_PB_SCALE — particle scale (matches m_scale, default 1.0)
    P1_PB_SCALE, _M("Scale"), TYPE_FLOAT, 0, IDS_P1_PARAM_SCALE,
        p_default,  1.0f,
        p_range,    0.001f, 500.0f,
        p_ui,       P1_MAP_MODEL, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_P1_EDIT_SCALE, IDC_P1_SPIN_SCALE, 0.1f,
    p_end,

    p_end   // Final terminator
);

// ============================================================================
// Utility functions
// ============================================================================

const MCHAR* GetString(UINT id)
{
    if (!hInstance)
        return nullptr;
    if (!::LoadString(hInstance, id, s_stringBuf, _countof(s_stringBuf)))
        return nullptr;
    return s_stringBuf;
}

ClassDesc2* GetWc3Particles1Desc()
{
    return &p1Desc;
}

// ============================================================================
// DLL entry points
// ============================================================================

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID /*lpvReserved*/)
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        hInstance = hinstDLL;
        InitCommonControls();
    }
    return TRUE;
}

extern "C" __declspec(dllexport) const TCHAR* LibDescription()
{
    return GetString(IDS_P1_LIB_DESCRIPTION);
}

extern "C" __declspec(dllexport) int LibNumberClasses()
{
    return 1;
}

extern "C" __declspec(dllexport) ClassDesc* LibClassDesc(int i)
{
    return (i == 0) ? GetWc3Particles1Desc() : nullptr;
}

extern "C" __declspec(dllexport) ULONG LibVersion()
{
    return VERSION_3DSMAX;
}

extern "C" __declspec(dllexport) ULONG CanAutoDefer()
{
    return 1;
}

// ============================================================================
// Wc3Particles1ClassDesc implementation
// ============================================================================

int Wc3Particles1ClassDesc::IsPublic()
{
    return 1;
}

void* Wc3Particles1ClassDesc::Create(BOOL /*loading*/)
{
    return new Wc3Particles1Particle();
}

const TCHAR* Wc3Particles1ClassDesc::ClassName()
{
    return GetString(IDS_P1_CLASS_NAME);
}

#if MAX_PRODUCT_YEAR_NUMBER >= 2022
const MCHAR* Wc3Particles1ClassDesc::NonLocalizedClassName()
{
    return GetString(IDS_P1_CLASS_NAME);
}
#endif

SClass_ID Wc3Particles1ClassDesc::SuperClassID()
{
    return GEOMOBJECT_CLASS_ID;
}

Class_ID Wc3Particles1ClassDesc::ClassID()
{
    return WC3PARTICLES1_CLASS_ID;
}

const TCHAR* Wc3Particles1ClassDesc::Category()
{
    return GetString(IDS_P1_CATEGORY);
}

const TCHAR* Wc3Particles1ClassDesc::InternalName()
{
    return _M("Wc3Particles1Emitter");
}

HINSTANCE Wc3Particles1ClassDesc::HInstance()
{
    return hInstance;
}

// ============================================================================
// Emitter1CreateCallback — single-click creation (point emitter)
// ============================================================================

int Emitter1CreateCallback::proc(ViewExp* vpt, int msg, int point, int flags,
                                 IPoint2 m, Matrix3& mat)
{
    if (msg == MOUSE_FREEMOVE) {
        vpt->SnapPreview(m, m, nullptr, SNAP_IN_PLANE);
    }
    if (msg != MOUSE_POINT && msg != MOUSE_MOVE) {
        return (msg == MOUSE_ABORT) ? CREATE_ABORT : CREATE_CONTINUE;
    }

    if (point == 0) {
        Point3 p0 = vpt->SnapPoint(m, m, nullptr, SNAP_IN_PLANE);
        mat.SetTrans(p0);
        if (msg == MOUSE_POINT)
            return CREATE_STOP;
    }
    return CREATE_CONTINUE;
}

// ============================================================================
// Controller type utilities (shared with Wc3Particles2 pattern)
// ============================================================================

static const MCHAR* s_ctrlTypeNames[] = { _M("None"), _M("Linear"), _M("Bezier"), _M("Hermite") };
static const int    s_ctrlTypeCount = 4;

static int DetectControllerType(IParamBlock2* pb, ParamID pid)
{
    int tabIndex = pb->GetDesc()->IDtoIndex(pid);
    Control* ctrl = pb->GetControllerByIndex(tabIndex);
    if (!ctrl) return 0;
    Class_ID cid = ctrl->ClassID();
    if (cid == Class_ID(LININTERP_FLOAT_CLASS_ID, 0)) return 1;
    if (cid == Class_ID(HYBRIDINTERP_FLOAT_CLASS_ID, 0)) return 2;
    if (cid == Class_ID(TCBINTERP_FLOAT_CLASS_ID, 0)) return 3;
    return 2;
}

static void SetupCtrlCombo(HWND hWnd, int comboID, IParamBlock2* pb, ParamID pid)
{
    HWND hCombo = GetDlgItem(hWnd, comboID);
    if (!hCombo) return;
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < s_ctrlTypeCount; i++)
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)s_ctrlTypeNames[i]);
    SendMessage(hCombo, CB_SETCURSEL, DetectControllerType(pb, pid), 0);
}

static void SwitchControllerType(IParamBlock2* pb, ParamID pid, int type)
{
    int tabIndex = pb->GetDesc()->IDtoIndex(pid);
    Control* newCtrl = nullptr;
    switch (type) {
    case 0:
        pb->SetControllerByIndex(tabIndex, 0, nullptr);
        return;
    case 1:
        newCtrl = static_cast<Control*>(
            CreateInstance(CTRL_FLOAT_CLASS_ID, Class_ID(LININTERP_FLOAT_CLASS_ID, 0)));
        break;
    case 2:
        newCtrl = NewDefaultFloatController();
        break;
    case 3:
        newCtrl = static_cast<Control*>(
            CreateInstance(CTRL_FLOAT_CLASS_ID, Class_ID(TCBINTERP_FLOAT_CLASS_ID, 0)));
        break;
    }
    if (newCtrl)
        pb->SetControllerByIndex(tabIndex, 0, newCtrl);
}

struct CtrlComboMapping { int comboID; ParamID paramID; };
static const CtrlComboMapping s_p1CtrlMappings[] = {
    { IDC_P1_CTRL_SPEED,     P1_PB_SPEED },
    { IDC_P1_CTRL_LATITUDE,  P1_PB_LATITUDE },
    { IDC_P1_CTRL_LONGITUDE, P1_PB_LONGITUDE },
    { IDC_P1_CTRL_ACCEL,     P1_PB_ACCELERATION },
    { IDC_P1_CTRL_EMISSION,  P1_PB_EMISSION_RATE },
};
static constexpr int s_p1CtrlMappingCount = static_cast<int>(std::size(s_p1CtrlMappings));

// ============================================================================
// Wc3Particles1DlgProc implementation
// ============================================================================

void Wc3Particles1DlgProc::SetupMaxRate(HWND hWnd, IParamBlock2* pb, TimeValue t)
{
    float life = 0.0f;
    float emission = 0.0f;
    Interval forever = FOREVER;

    pb->GetValue(P1_PB_LIFE, t, life, forever);
    pb->GetValue(P1_PB_EMISSION_RATE, t, emission, forever);

    int count = 0;
    if (life > 0.0f && emission > 0.0f)
        count = std::min(static_cast<int>(emission * life), P1_PARAM_COUNT_MAX);
    pb->SetValue(P1_PB_COUNT, t, count);

    ISpinnerControl* spin = GetISpinner(GetDlgItem(hWnd, IDC_P1_SPIN_COUNT));
    if (spin) {
        spin->SetValue(count, FALSE);
        ReleaseISpinner(spin);
    }
}

void Wc3Particles1DlgProc::BrowseForModelFile(HWND hWnd)
{
    TCHAR fileBuf[MAX_PATH] = _T("");
    OPENFILENAME ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hWnd;
    ofn.lpstrFilter  = _T("Model Files\0*.mdl;*.mdx\0All Files\0*.*\0");
    ofn.lpstrFile    = fileBuf;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt  = _T("mdl");

    if (GetOpenFileName(&ofn)) {
        TCHAR uncBuf[1024];
        DWORD uncSize = sizeof(uncBuf);
        if (WNetGetUniversalName(fileBuf, UNIVERSAL_NAME_INFO_LEVEL,
                                 uncBuf, &uncSize) == NO_ERROR) {
            auto* uni = reinterpret_cast<UNIVERSAL_NAME_INFO*>(uncBuf);
            _tcscpy_s(fileBuf, uni->lpUniversalName);
        }
        po->m_modelPath = fileBuf;

        ICustEdit* ce = GetICustEdit(GetDlgItem(hWnd, IDC_P1_CUSTOMEDIT_PATH));
        if (ce) {
            ce->SetText(fileBuf);
            ReleaseICustEdit(ce);
        }
    }
}

INT_PTR Wc3Particles1DlgProc::DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                                       UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
    {
        IParamBlock2* pb = map->GetParamBlock();
        SetupMaxRate(hWnd, pb, t);

        // Disable the count spinner — auto-computed
        {
            ISpinnerControl* spin = GetISpinner(GetDlgItem(hWnd, IDC_P1_SPIN_COUNT));
            if (spin) {
                spin->Disable();
                ReleaseISpinner(spin);
            }
            ICustEdit* countEdit = GetICustEdit(GetDlgItem(hWnd, IDC_P1_EDIT_COUNT));
            if (countEdit) {
                countEdit->Disable();
                ReleaseICustEdit(countEdit);
            }
        }

        // Set model path edit control
        ICustEdit* ce = GetICustEdit(GetDlgItem(hWnd, IDC_P1_CUSTOMEDIT_PATH));
        if (ce) {
            ce->SetText(po->m_modelPath.data());
            ReleaseICustEdit(ce);
        }

        // Set model path prefix edit control
        {
            ICustEdit* cePrefix = GetICustEdit(GetDlgItem(hWnd, IDC_P1_EDIT_PATH_PREFIX));
            if (cePrefix) {
                if (po->m_modelPrefix.isNull() || po->m_modelPrefix.length() == 0)
                    po->m_modelPrefix = _M("");
                cePrefix->SetText(po->m_modelPrefix.data());
                ReleaseICustEdit(cePrefix);
            }
        }

        // Populate controller-type comboboxes
        for (int m = 0; m < s_p1CtrlMappingCount; m++)
            SetupCtrlCombo(hWnd, s_p1CtrlMappings[m].comboID, pb, s_p1CtrlMappings[m].paramID);

        return TRUE;
    }

    case WM_COMMAND:
    {
        int hiWord = HIWORD(wParam);
        if (hiWord == BN_CLICKED || hiWord == 0) {
            if (LOWORD(wParam) == IDC_P1_BUTTON_BROWSE)
                BrowseForModelFile(hWnd);
            return TRUE;
        }
        if (hiWord == CBN_SELCHANGE) {
            for (int m = 0; m < s_p1CtrlMappingCount; m++) {
                if (LOWORD(wParam) == s_p1CtrlMappings[m].comboID) {
                    auto sel = SendMessage(reinterpret_cast<HWND>(lParam),
                                           CB_GETCURSEL, 0, 0);
                    if (sel != CB_ERR)
                        SwitchControllerType(map->GetParamBlock(),
                                             s_p1CtrlMappings[m].paramID,
                                             static_cast<int>(sel));
                    break;
                }
            }
            return TRUE;
        }
        break;
    }

    case CC_SPINNER_CHANGE:
    {
        int spinID = LOWORD(wParam);
        if (spinID == IDC_P1_SPIN_LIFE || spinID == IDC_P1_SPIN_EMISSION)
            SetupMaxRate(hWnd, map->GetParamBlock(), t);
        break;
    }

    case WM_CUSTEDIT_ENTER:
    {
        if (LOWORD(wParam) == IDC_P1_CUSTOMEDIT_PATH) {
            MCHAR buf[MAX_PATH] = {};
            ICustEdit* ce = GetICustEdit(reinterpret_cast<HWND>(lParam));
            if (ce) {
                ce->GetText(buf, MAX_PATH);
                po->m_modelPath = buf;
                ReleaseICustEdit(ce);
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_P1_EDIT_PATH_PREFIX) {
            MCHAR buf[MAX_PATH] = {};
            ICustEdit* ce = GetICustEdit(reinterpret_cast<HWND>(lParam));
            if (ce) {
                ce->GetText(buf, MAX_PATH);
                po->m_modelPrefix = buf;
                ReleaseICustEdit(ce);
            }
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// ============================================================================
// Config1DlgProc — Import/Export
// ============================================================================

static std::string NarrowPath(const MCHAR* widePath) {
    int len = WideCharToMultiByte(CP_UTF8, 0, widePath, -1, nullptr, 0, nullptr, nullptr);
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, widePath, -1, &result[0], len, nullptr, nullptr);
    if (!result.empty() && result.back() == 0) result.pop_back();
    return result;
}

static MSTR WidenPath(const std::string& narrow) {
    if (narrow.empty()) return MSTR();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, wbuf.data(), wlen);
    return MSTR(wbuf.data());
}

static bool LoadAnsiChunk(ILoad* iload, MSTR& out) {
    ULONG nb = 0;
    int len = 0;
    IOResult res = iload->Read(&len, sizeof(int), &nb);
    if (res != IO_OK || len <= 0) return false;
    std::vector<char> buf(len);
    res = iload->Read(buf.data(), len, &nb);
    if (res != IO_OK) return false;
    int wlen = MultiByteToWideChar(CP_ACP, 0, buf.data(), len, nullptr, 0);
    if (wlen > 0) {
        std::wstring wpath(wlen, L'\0');
        MultiByteToWideChar(CP_ACP, 0, buf.data(), len, wpath.data(), wlen);
        out = wpath.c_str();
    }
    return true;
}

static void IniWrite(FILE* f, const char* key, const char* val) {
    fprintf(f, "%s=%s\n", key, val);
}
static void IniWriteFloat(FILE* f, const char* key, float val) {
    char buf[64]; sprintf_s(buf, "%.6g", val);
    IniWrite(f, key, buf);
}
static void IniWriteInt(FILE* f, const char* key, int val) {
    char buf[64]; sprintf_s(buf, "%d", val);
    IniWrite(f, key, buf);
}

static std::map<std::string, std::string> IniReadSection(const char* filename) {
    std::map<std::string, std::string> result;
    FILE* f = nullptr;
    fopen_s(&f, filename, "r");
    if (!f) return result;
    char line[4096];
    bool inSection = false;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (line[0] == '[') {
            inSection = (strncmp(line, "[Wc3Particles1]", 15) == 0);
            continue;
        }
        if (!inSection) continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        result[std::string(line)] = std::string(eq + 1);
    }
    fclose(f);
    return result;
}

static float IniGetFloat(const std::map<std::string, std::string>& ini, const char* key, float def) {
    auto it = ini.find(key);
    if (it == ini.end() || it->second.empty()) return def;
    return static_cast<float>(atof(it->second.c_str()));
}
static int IniGetInt(const std::map<std::string, std::string>& ini, const char* key, int def) {
    auto it = ini.find(key);
    if (it == ini.end() || it->second.empty()) return def;
    return atoi(it->second.c_str());
}
static std::string IniGetStr(const std::map<std::string, std::string>& ini, const char* key) {
    auto it = ini.find(key);
    if (it == ini.end()) return "";
    return it->second;
}

static void ExportAnimFloat(FILE* f, const char* key, const char* animKey,
                            IParamBlock2* pb, ParamID pid, TimeValue t)
{
    int tabIndex = pb->GetDesc()->IDtoIndex(pid);
    Control* ctrl = pb->GetControllerByIndex(tabIndex);

    if (ctrl && ctrl->NumKeys() > 1) {
        IniWrite(f, animKey, "True");
        std::string keyStr;
        for (int i = 0; i < ctrl->NumKeys(); i++) {
            TimeValue keyTime = ctrl->GetKeyTime(i);
            float val = 0.0f;
            Interval iv;
            ctrl->GetValue(keyTime, &val, iv);
            int frame = keyTime / GetTicksPerFrame();
            char buf[64];
            sprintf_s(buf, "%df %g", frame, val);
            if (i > 0) keyStr += "|";
            keyStr += buf;
        }
        IniWrite(f, key, keyStr.c_str());
    } else {
        IniWrite(f, animKey, "False");
        float fVal = 0.0f;
        Interval iv(TIME_NegInfinity, TIME_PosInfinity);
        pb->GetValue(pid, t, fVal, iv);
        IniWriteFloat(f, key, fVal);
    }
}

static void ImportAnimFloat(const std::map<std::string, std::string>& ini,
                            const char* key, const char* animKey,
                            IParamBlock2* pb, ParamID pid, float def,
                            bool loadDynamic)
{
    std::string animVal = IniGetStr(ini, animKey);
    bool hasAnim = (animVal == "True");

    if (hasAnim && loadDynamic) {
        std::string data = IniGetStr(ini, key);
        if (data.empty()) return;

        int tabIndex = pb->GetDesc()->IDtoIndex(pid);
        pb->SetControllerByIndex(tabIndex, 0, NewDefaultFloatController());

        SuspendAnimate();
        AnimateOn();

        char buf[4096];
        strncpy_s(buf, data.c_str(), sizeof(buf) - 1);
        char* context = nullptr;
        char* token = strtok_s(buf, "|", &context);
        while (token) {
            int frame = 0; float val = 0.0f;
            if (sscanf_s(token, "%df %f", &frame, &val) == 2) {
                TimeValue keyTime = frame * GetTicksPerFrame();
                pb->SetValue(pid, keyTime, val);
            }
            token = strtok_s(nullptr, "|", &context);
        }

        ResumeAnimate();
    } else {
        float val = IniGetFloat(ini, key, def);
        pb->SetValue(pid, 0, val);
    }
}

void Config1DlgProc::ExportConfig(HWND hWnd, IParamBlock2* pb, TimeValue t)
{
    OPENFILENAME ofn = {};
    MCHAR szFile[MAX_PATH] = _M("");
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hWnd;
    ofn.lpstrFilter  = _M("Config File (*.ini)\0*.ini\0All Files\0*.*\0");
    ofn.lpstrFile    = szFile;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt  = _M("ini");
    ofn.lpstrTitle   = _M("Export Particle1 Configuration");

    if (!GetSaveFileName(&ofn)) return;

    std::string path = NarrowPath(szFile);
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "w");
    if (!f) { MessageBox(hWnd, _M("Could not create file."), _M("Export Error"), MB_OK); return; }

    fprintf(f, "[Wc3Particles1]\n");

    Interval iv(TIME_NegInfinity, TIME_PosInfinity);
    int iVal; float fVal;

    // Emitter
    pb->GetValue(P1_PB_COUNT, t, iVal, iv); IniWriteInt(f, "Count", iVal);
    ExportAnimFloat(f, "Speed",     "SpeedUseAnim",     pb, P1_PB_SPEED, t);
    ExportAnimFloat(f, "Latitude",  "LatitudeUseAnim",  pb, P1_PB_LATITUDE, t);
    ExportAnimFloat(f, "Longitude", "LongitudeUseAnim", pb, P1_PB_LONGITUDE, t);
    ExportAnimFloat(f, "Gravity",   "GravityUseAnim",   pb, P1_PB_ACCELERATION, t);

    // Timing
    pb->GetValue(P1_PB_LIFE, t, fVal, iv); IniWriteFloat(f, "Life", fVal);
    ExportAnimFloat(f, "EmissionRate", "EmissionRateUseAnim", pb, P1_PB_EMISSION_RATE, t);

    // Model
    pb->GetValue(P1_PB_SCALE, t, fVal, iv); IniWriteFloat(f, "Scale", fVal);
    {
        std::string narrowPfx = NarrowPath(po->m_modelPrefix.data());
        IniWrite(f, "PreFix", narrowPfx.c_str());
        std::string narrowPath = NarrowPath(po->m_modelPath.data());
        IniWrite(f, "File", narrowPath.c_str());
    }

    fclose(f);
    MessageBox(hWnd, _M("Configuration exported successfully."), _M("Wc3Particles1"), MB_OK | MB_ICONINFORMATION);
}

void Config1DlgProc::ImportConfig(HWND hWnd, IParamBlock2* pb, TimeValue t)
{
    OPENFILENAME ofn = {};
    MCHAR szFile[MAX_PATH] = _M("");
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hWnd;
    ofn.lpstrFilter  = _M("Config File (*.ini)\0*.ini\0All Files\0*.*\0");
    ofn.lpstrFile    = szFile;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle   = _M("Import Particle1 Configuration");

    if (!GetOpenFileName(&ofn)) return;

    std::string path = NarrowPath(szFile);
    auto ini = IniReadSection(path.c_str());

    if (ini.empty()) {
        MessageBox(hWnd, _M("No [Wc3Particles1] section found in this file."),
                   _M("Import Error"), MB_OK | MB_ICONWARNING);
        return;
    }

    bool dynLoad = (loadDynamic != FALSE);
    theHold.Begin();

    // Emitter
    pb->SetValue(P1_PB_COUNT, t, IniGetInt(ini, "Count", 500));
    ImportAnimFloat(ini, "Speed",     "SpeedUseAnim",     pb, P1_PB_SPEED,        10.0f, dynLoad);
    ImportAnimFloat(ini, "Latitude",  "LatitudeUseAnim",  pb, P1_PB_LATITUDE,     45.0f, dynLoad);
    ImportAnimFloat(ini, "Longitude", "LongitudeUseAnim", pb, P1_PB_LONGITUDE,    45.0f, dynLoad);
    ImportAnimFloat(ini, "Gravity",   "GravityUseAnim",   pb, P1_PB_ACCELERATION,  0.0f, dynLoad);

    // Timing
    pb->SetValue(P1_PB_LIFE, t, IniGetFloat(ini, "Life", 1.0f));
    ImportAnimFloat(ini, "EmissionRate", "EmissionRateUseAnim", pb, P1_PB_EMISSION_RATE, 50.0f, dynLoad);

    // Model
    pb->SetValue(P1_PB_SCALE, t, IniGetFloat(ini, "Scale", 1.0f));
    {
        std::string pfx = IniGetStr(ini, "PreFix");
        if (!pfx.empty())
            po->m_modelPrefix = WidenPath(pfx);
        std::string mdl = IniGetStr(ini, "File");
        if (!mdl.empty())
            po->m_modelPath = WidenPath(mdl);
    }

    theHold.Accept(_M("Import Particle1 Config"));
    pb->GetDesc()->InvalidateUI();

    MessageBox(hWnd, _M("Configuration imported successfully."), _M("Wc3Particles1"), MB_OK | MB_ICONINFORMATION);
}

INT_PTR Config1DlgProc::DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                                 UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        CheckDlgButton(hWnd, IDC_P1_CHECK_LOAD_DYNAMIC, loadDynamic ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_P1_CHECK_LOAD_DYNAMIC) {
            loadDynamic = IsDlgButtonChecked(hWnd, IDC_P1_CHECK_LOAD_DYNAMIC);
            return TRUE;
        }
        if (HIWORD(wParam) == BN_CLICKED) {
            IParamBlock2* pb = map->GetParamBlock();
            if (LOWORD(wParam) == IDC_P1_BUTTON_EXPORT)
                ExportConfig(hWnd, pb, t);
            else if (LOWORD(wParam) == IDC_P1_BUTTON_IMPORT)
                ImportConfig(hWnd, pb, t);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// ============================================================================
// Wc3Particles1ParticleDraw — draws a wireframe diamond at each particle
// ============================================================================

BOOL Wc3Particles1ParticleDraw::DrawParticle(GraphicsWindow* gw, ParticleSys& parts, int i)
{
    float sz = scale;
    if (sz < 0.001f)
        return (GetAsyncKeyState(VK_ESCAPE) != 0);

    // Draw a 3D wireframe cross at the particle position
    Point3 p = parts[i];
    Point3 cross[2];

    // X axis
    cross[0] = p + Point3(sz, 0.0f, 0.0f);
    cross[1] = p - Point3(sz, 0.0f, 0.0f);
    gw->polyline(2, cross, nullptr, nullptr, FALSE, nullptr);

    // Y axis
    cross[0] = p + Point3(0.0f, sz, 0.0f);
    cross[1] = p - Point3(0.0f, sz, 0.0f);
    gw->polyline(2, cross, nullptr, nullptr, FALSE, nullptr);

    // Z axis
    cross[0] = p + Point3(0.0f, 0.0f, sz);
    cross[1] = p - Point3(0.0f, 0.0f, sz);
    gw->polyline(2, cross, nullptr, nullptr, FALSE, nullptr);

    return (GetAsyncKeyState(VK_ESCAPE) != 0);
}

// ============================================================================
// Helper functions
// ============================================================================

void ParticleCacheData(ParticleSys* dst, ParticleSys* src)
{
    int pc = src->points.Count();
    dst->points.SetCount(pc);
    for (int i = 0; i < pc; i++)
        dst->points[i] = src->points[i];
    dst->ages.SetCount(src->ages.Count());
    for (int i = 0; i < src->ages.Count(); i++)
        dst->ages[i] = src->ages[i];
    dst->radius.SetCount(src->radius.Count());
    for (int i = 0; i < src->radius.Count(); i++)
        dst->radius[i] = src->radius[i];
    dst->tension.SetCount(src->tension.Count());
    for (int i = 0; i < src->tension.Count(); i++)
        dst->tension[i] = src->tension[i];
}

BOOL Parity(Matrix3* tm)
{
    Point3 cross = CrossProd(tm->GetRow(0), tm->GetRow(1));
    return DotProd(cross, tm->GetRow(2)) < 0.0f;
}

void FlipAllMeshFaces(Mesh* mesh)
{
    for (int i = 0; i < mesh->getNumFaces(); i++)
        mesh->FlipNormal(i);
}

// ============================================================================
// GenParticle1 implementation
// ============================================================================

GenParticle1::GenParticle1()
    : SimpleParticle()
{
    stepSize = 0;
}

GenParticle1::~GenParticle1() = default;

// --- PB2 reference management ---

int GenParticle1::NumRefs() { return 1; }

RefTargetHandle GenParticle1::GetReference(int i)
{
    return (i == 0) ? pblock2 : nullptr;
}

void GenParticle1::SetReference(int i, RefTargetHandle rtarg)
{
    if (i == 0) {
        pblock2 = static_cast<IParamBlock2*>(rtarg);
        pblock = reinterpret_cast<IParamBlock*>(rtarg);
    }
}

int GenParticle1::NumSubs() { return 1; }

Animatable* GenParticle1::SubAnim(int i)
{
    return (i == 0) ? pblock2 : nullptr;
}

#if MAX_PRODUCT_YEAR_NUMBER >= 2022
MSTR GenParticle1::SubAnimName(int i, bool localized)
#else
MSTR GenParticle1::SubAnimName(int i)
#endif
{
    return (i == 0) ? MSTR(_M("Parameters")) : MSTR();
}

int GenParticle1::NumParamBlocks() { return 1; }

IParamBlock2* GenParticle1::GetParamBlock(int i)
{
    return (i == 0) ? pblock2 : nullptr;
}

IParamBlock2* GenParticle1::GetParamBlockByID(BlockID id)
{
    return (id == wc3particles1_params) ? pblock2 : nullptr;
}

RefResult GenParticle1::NotifyRefChanged(const Interval&, RefTargetHandle,
                                         PartID&, RefMessage message, BOOL)
{
    if (message == REFMSG_CHANGE)
        NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
    return REF_SUCCEED;
}

// --- Particle system core ---

int GenParticle1::GetParticleCount()
{
    int count = 0;
    Interval forever = FOREVER;
    pblock2->GetValue(P1_PB_COUNT, 0, count, forever);
    return std::min(count, P1_PARAM_COUNT_MAX);
}

int GenParticle1::CountLive()
{
    int c = 0;
    for (int i = 0; i < parts.Count(); i++) {
        if (parts.Alive(i))
            c++;
    }
    return c;
}

void GenParticle1::ComputeParticleStart(TimeValue t0, INode* /*node*/)
{
    int count = GetParticleCount();
    parts.SetCount(count, PARTICLE_VELS | PARTICLE_AGES);
    for (int i = 0; i < count; i++)
        parts.ages[i] = -1;
    tvalid = t0;
    valid  = TRUE;
}

/**
 * @brief Births a particle using the CParticleEmitter cone-emission algorithm.
 *
 * Faithful to the pseudocode:
 *   1. Position at emitter origin (world space)
 *   2. Velocity along +Z, rotated by random theta (latitude) and phi (longitude)
 *   3. Transform velocity to world space via the emitter matrix
 */
void GenParticle1::BirthParticle(INode* node, TimeValue bt, int index)
{
    float speed = 0.0f, lat = 0.0f, lon = 0.0f;
    Interval forever = FOREVER;

    pblock2->GetValue(P1_PB_SPEED, bt, speed, forever);
    pblock2->GetValue(P1_PB_LATITUDE, bt, lat, forever);
    pblock2->GetValue(P1_PB_LONGITUDE, bt, lon, forever);

    constexpr float kDegToRad = 0.017453292f;
    float latRad = lat * kDegToRad;
    float lonRad = lon * kDegToRad;

    Matrix3 tm = node->GetObjTMBeforeWSM(bt);

    // Unit conversion matching Wc3Particles2 convention
    speed /= -static_cast<float>(P1_TICKS_PER_SEC);

    // Initial velocity along +Z (after double negation: -(-speed) = +speed)
    Point3 vel(0.0f, 0.0f, -speed);

    // Random emission angles within the latitude/longitude cone
    // Matches CParticleEmitter::CreateParticle pseudocode
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    auto randFloat = [&]() { return dist01(m_rng); };

    float theta = (2.0f * latRad * randFloat()) - latRad;
    float phi   = (2.0f * lonRad * randFloat()) - lonRad;

    // Rotate velocity by theta (around Y) — latitude spread
    float sinTheta = sinf(theta), cosTheta = cosf(theta);
    vel.x = vel.z * sinTheta;
    vel.z = vel.z * cosTheta;

    // Rotate velocity by phi (around Z) — longitude spread
    float sinPhi = sinf(phi), cosPhi = cosf(phi);
    vel.y = vel.x * sinPhi;
    vel.x = vel.x * cosPhi;

    // Transform velocity direction to world space
    vel = VectorTransform(tm, vel);

    parts.ages[index] = 0;
    parts.vels[index] = vel;

    // Position at emitter origin in world space
    parts[index] = Point3(0.0f, 0.0f, 0.0f) * tm;
}

/**
 * @brief Main particle simulation update.
 *
 * Based on CParticleEmitter with Euler integration and -Z gravity.
 */
void GenParticle1::UpdateParticles(TimeValue t, INode* node)
{
    if (node && (node->IsNodeHidden() || node->GetVisibility(t) <= 0.0f)) {
        parts.FreeAll();
        tvalid = t;
        valid  = FALSE;
        return;
    }

    constexpr int PARTICLE_SEED = static_cast<int>(0x9e3779b9);

    TimeValue t0 = 0;
    float life_sec = 0.0f;
    int total = 0;
    int birth = 0;
    float brate = 1.0f;

    TimeValue oneframe = GetTicksPerFrame();
    if (stepSize != oneframe) {
        stepSize = oneframe;
        valid = FALSE;
    }

    Interval forever = FOREVER;
    pblock2->GetValue(P1_PB_LIFE, t, life_sec, forever);
    TimeValue life = static_cast<TimeValue>(life_sec * static_cast<float>(P1_TICKS_PER_SEC));
    total = GetParticleCount();

    if (life <= 0)
        life = 1;

    if (t < t0) {
        parts.FreeAll();
        tvalid = t;
        valid  = TRUE;
        return;
    }

    if (!valid || t < tvalid || tvalid < t0) {
        ComputeParticleStart(t0, node);
    }
    valid = TRUE;

    if (!TestAFlag(A_PLUGIN1)) {
        int offby = t % oneframe;
        if (offby > 0)
            t -= offby;
    }

    while (tvalid < t) {
        int born = 0;
        TimeValue dt;

        if (tvalid % stepSize != 0)
            dt = stepSize - tvalid % stepSize;
        else
            dt = stepSize;
        if (dt + tvalid > t)
            dt = t - tvalid;

        tvalid += dt;

        BOOL fullframe = (tvalid % oneframe == 0);
        if (fullframe) {
            pblock2->GetValue(P1_PB_EMISSION_RATE, tvalid, brate, forever);
            brate /= static_cast<float>(P1_TICKS_PER_SEC);
            birth = static_cast<int>(static_cast<float>(tvalid - t0) * brate)
                  - static_cast<int>(static_cast<float>(tvalid - t0 - dt) * brate);
        }

        // Age existing particles; kill those past lifetime
        for (int j = 0; j < parts.Count(); j++) {
            if (!parts.Alive(j)) continue;
            parts.ages[j] += dt;
            if (parts.ages[j] >= life)
                parts.ages[j] = -1;
        }

        // Deterministic RNG seed (same Perm table approach as Wc3Particles2)
        int seed1 = 1200 * tvalid / P1_TICKS_PER_SEC;
        int seed2 = seed1 >> 8;
        int seed3 = seed2 >> 8;
        int seed4 = seed3 >> 8;
        seed1 &= 0xFF;
        seed2 &= 0xFF;
        seed3 &= 0xFF;
        seed4 &= 0xFF;
        std::mt19937::result_type combinedSeed = static_cast<std::mt19937::result_type>(
            (Perm(seed1) << 24) +
            (Perm(seed2) << 16) +
            (Perm(seed3) << 8)  +
            Perm(seed4) +
            PARTICLE_SEED);
        m_rng.seed(combinedSeed);

        // Birth new particles
        for (int j = 0; j < parts.Count(); j++) {
            if (born >= birth) break;
            if (!parts.Alive(j)) {
                BirthParticle(node, tvalid, j);
                born++;
            }
        }

        // Apply force fields on full frames
        if (fullframe) {
            Point3 force;
            for (int i = 0; i < fields.Count(); i++) {
                for (int m = 0; m < parts.Count(); m++) {
                    if (!parts.Alive(m)) continue;
                    force = fields[i]->Force(tvalid, parts[m], parts.vels[m], m);
                    parts.vels[m] += force * static_cast<float>(dt);
                }
            }
        }

        // Apply gravity acceleration along -Z (matches CParticleEmitter::MoveParticle)
        if (fullframe) {
            float accel = 0.0f;
            pblock2->GetValue(P1_PB_ACCELERATION, tvalid, accel, forever);
            if (accel != 0.0f) {
                float gAccel = -accel / (static_cast<float>(P1_TICKS_PER_SEC) * static_cast<float>(P1_TICKS_PER_SEC));
                for (int g = 0; g < parts.Count(); g++) {
                    if (!parts.Alive(g)) continue;
                    parts.vels[g].z += gAccel * static_cast<float>(dt);
                }
            }
        }

        // Integrate positions
        for (int n = 0; n < parts.Count(); n++) {
            if (!parts.Alive(n)) continue;
            parts[n] += parts.vels[n] * static_cast<float>(dt);
        }
    }

    assert(tvalid == t);
}

/**
 * @brief Builds the emitter mesh: an octahedron + direction arrow.
 *
 * Since this is a point emitter (no rectangular plane), the emitter is
 * visualised as a small wireframe octahedron with a +Z arrow showing
 * the primary emission direction.
 */
void GenParticle1::BuildEmitter(TimeValue t, Mesh& mesh)
{
    mvalid = Interval(TIME_NegInfinity, TIME_PosInfinity);

    constexpr float r = 5.0f;    // octahedron radius
    constexpr float az = 15.0f;  // arrow length

    // 7 verts: 6 octahedron + arrow tip
    mesh.setNumVerts(7);
    mesh.setNumFaces(10);

    mesh.setVert(0, Point3( r, 0.0f, 0.0f));   // +X
    mesh.setVert(1, Point3(-r, 0.0f, 0.0f));   // -X
    mesh.setVert(2, Point3(0.0f,  r, 0.0f));   // +Y
    mesh.setVert(3, Point3(0.0f, -r, 0.0f));   // -Y
    mesh.setVert(4, Point3(0.0f, 0.0f,  r));   // +Z
    mesh.setVert(5, Point3(0.0f, 0.0f, -r));   // -Z
    mesh.setVert(6, Point3(0.0f, 0.0f, az));   // arrow tip

    // Upper 4 faces (to +Z vertex)
    mesh.faces[0].setEdgeVisFlags(1, 1, 0); mesh.faces[0].setSmGroup(1); mesh.faces[0].setVerts(0, 2, 4);
    mesh.faces[1].setEdgeVisFlags(1, 1, 0); mesh.faces[1].setSmGroup(1); mesh.faces[1].setVerts(2, 1, 4);
    mesh.faces[2].setEdgeVisFlags(1, 1, 0); mesh.faces[2].setSmGroup(1); mesh.faces[2].setVerts(1, 3, 4);
    mesh.faces[3].setEdgeVisFlags(1, 1, 0); mesh.faces[3].setSmGroup(1); mesh.faces[3].setVerts(3, 0, 4);

    // Lower 4 faces (to -Z vertex)
    mesh.faces[4].setEdgeVisFlags(1, 0, 1); mesh.faces[4].setSmGroup(1); mesh.faces[4].setVerts(0, 5, 2);
    mesh.faces[5].setEdgeVisFlags(1, 0, 1); mesh.faces[5].setSmGroup(1); mesh.faces[5].setVerts(2, 5, 1);
    mesh.faces[6].setEdgeVisFlags(1, 0, 1); mesh.faces[6].setSmGroup(1); mesh.faces[6].setVerts(1, 5, 3);
    mesh.faces[7].setEdgeVisFlags(1, 0, 1); mesh.faces[7].setSmGroup(1); mesh.faces[7].setVerts(3, 5, 0);

    // Arrow shaft (degenerate face for display)
    mesh.faces[8].setEdgeVisFlags(1, 1, 0); mesh.faces[8].setSmGroup(1); mesh.faces[8].setVerts(4, 6, 4);
    mesh.faces[9].setEdgeVisFlags(1, 0, 0); mesh.faces[9].setSmGroup(1); mesh.faces[9].setVerts(6, 4, 4);

    mesh.InvalidateGeomCache();
}

/**
 * @brief Draws the emitter octahedron and direction arrow as wireframe.
 */
static void DrawEmitter1Wireframe(GraphicsWindow* gw)
{
    constexpr float r = 5.0f;
    constexpr float az = 15.0f;

    // Equator ring
    Point3 equator[5];
    equator[0] = Point3( r, 0.0f, 0.0f);
    equator[1] = Point3(0.0f,  r, 0.0f);
    equator[2] = Point3(-r, 0.0f, 0.0f);
    equator[3] = Point3(0.0f, -r, 0.0f);
    equator[4] = equator[0];
    gw->polyline(5, equator, nullptr, nullptr, FALSE, nullptr);

    // Top connections (+Z to equator)
    Point3 top(0.0f, 0.0f, r);
    for (int i = 0; i < 4; i++) {
        Point3 seg[2] = { top, equator[i] };
        gw->polyline(2, seg, nullptr, nullptr, FALSE, nullptr);
    }

    // Bottom connections (-Z to equator)
    Point3 bot(0.0f, 0.0f, -r);
    for (int i = 0; i < 4; i++) {
        Point3 seg[2] = { bot, equator[i] };
        gw->polyline(2, seg, nullptr, nullptr, FALSE, nullptr);
    }

    // Direction arrow
    Point3 arrow[2] = { Point3(0.0f, 0.0f, 0.0f), Point3(0.0f, 0.0f, az) };
    gw->polyline(2, arrow, nullptr, nullptr, FALSE, nullptr);
}

int GenParticle1::Display(TimeValue t, INode* inode, ViewExp* vpt, int flags)
{
    if (!pblock2) return 0;

    GraphicsWindow* gw = vpt->getGW();
    DWORD rlim0 = gw->getRndLimits();

    // --- Emitter wireframe ---
    gw->setRndLimits(GW_WIREFRAME | GW_EDGES_ONLY | (rlim0 & GW_Z_BUFFER));

    Matrix3 tm = inode->GetObjectTM(t);
    gw->setTransform(tm);

    if (inode->Selected())
        gw->setColor(LINE_COLOR, GetSelColor());
    else if (inode->IsFrozen())
        gw->setColor(LINE_COLOR, GetFreezeColor());
    else
        gw->setColor(LINE_COLOR, Color(inode->GetWireColor()));

    DrawEmitter1Wireframe(gw);

    gw->setRndLimits(rlim0);

    // --- Particles (world space) ---
    Update(t, inode);
    MarkerType mt = GetMarkerType();
    gw->setTransform(Matrix3(1));
    parts.Render(gw, mt);

    return 0;
}

int GenParticle1::HitTest(TimeValue t, INode* inode, int type, int crossing,
                          int flags, IPoint2* p, ViewExp* vpt)
{
    if (!pblock2) return 0;

    GraphicsWindow* gw = vpt->getGW();
    HitRegion hitRegion;
    MakeHitRegion(hitRegion, type, crossing, 4, p);
    gw->setHitRegion(&hitRegion);

    Matrix3 tm = inode->GetObjectTM(t);
    gw->setTransform(tm);
    DWORD rlim0 = gw->getRndLimits();
    gw->setRndLimits(((rlim0 | GW_PICK) & ~GW_ILLUM) | GW_WIREFRAME);
    gw->clearHitCode();

    DrawEmitter1Wireframe(gw);

    if (gw->checkHitCode()) {
        gw->setRndLimits(rlim0);
        return TRUE;
    }

    UpdateMesh(t);
    if (mesh.select(gw, nullptr, &hitRegion)) {
        gw->setRndLimits(rlim0);
        return TRUE;
    }

    gw->setRndLimits(rlim0);
    return FALSE;
}

void GenParticle1::GetLocalBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box)
{
    constexpr float r = 5.0f;
    constexpr float az = 15.0f;
    box = Box3(Point3(-r, -r, -r), Point3(r, r, az));
    box.EnlargeBy(10.0f);
}

void GenParticle1::GetWorldBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box)
{
    GetLocalBoundBox(t, inode, vpt, box);
    Matrix3 tm = inode->GetObjectTM(t);
    box = box * tm;
}

Interval GenParticle1::GetValidity(TimeValue t)
{
    return Interval(t, t);
}

void GenParticle1::InvalidateUI()
{
    wc3particles1_param_blk.InvalidateUI();
}

ParamDimension* GenParticle1::GetParameterDim(int pbIndex)
{
    return defaultDim;
}

#if MAX_PRODUCT_YEAR_NUMBER >= 2022
MSTR GenParticle1::GetParameterName(int pbIndex, bool localized)
#else
MSTR GenParticle1::GetParameterName(int pbIndex)
#endif
{
    const MCHAR* s = nullptr;
    switch (pbIndex) {
    case P1_PB_COUNT:         s = GetString(IDS_P1_PARAM_COUNT); break;
    case P1_PB_SPEED:         s = GetString(IDS_P1_PARAM_SPEED); break;
    case P1_PB_EMISSION_RATE: s = GetString(IDS_P1_PARAM_EMISSION); break;
    case P1_PB_LIFE:          s = GetString(IDS_P1_PARAM_LIFE); break;
    case P1_PB_ACCELERATION:  s = GetString(IDS_P1_PARAM_ACCEL); break;
    case P1_PB_LATITUDE:      s = GetString(IDS_P1_PARAM_LATITUDE); break;
    case P1_PB_LONGITUDE:     s = GetString(IDS_P1_PARAM_LONGITUDE); break;
    case P1_PB_SCALE:         s = GetString(IDS_P1_PARAM_SCALE); break;
    default:                  break;
    }
    return MSTR(s ? s : _T(""));
}

Point3 GenParticle1::ParticlePosition(TimeValue /*t*/, int i)
{
    return parts.points[i];
}

Point3 GenParticle1::ParticleVelocity(TimeValue /*t*/, int i)
{
    return parts.vels[i];
}

int GenParticle1::ParticleLife(TimeValue t, int /*i*/)
{
    float life_sec = 0.0f;
    Interval forever = FOREVER;
    pblock2->GetValue(P1_PB_LIFE, t, life_sec, forever);
    return static_cast<int>(life_sec * P1_TICKS_PER_SEC);
}

int GenParticle1::RenderEnd(TimeValue /*t*/)
{
    ClearAFlag(A_PLUGIN1);
    ParticleInvalid();
    NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
    return 0;
}

int GenParticle1::RenderBegin(TimeValue /*t*/, ULONG /*flags*/)
{
    SetAFlag(A_PLUGIN1);
    ParticleInvalid();
    NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
    return 0;
}

void GenParticle1::RescaleWorldUnits(float f)
{
    if (TestAFlag(A_WORK1))
        return;
    SimpleParticle::RescaleWorldUnits(f);
}

CreateMouseCallBack* GenParticle1::GetCreateMouseCallBack()
{
    emitter1Callback.obj = this;
    return &emitter1Callback;
}

void GenParticle1::MapKeys(TimeMap* map, DWORD flags)
{
    Animatable::MapKeys(map, flags);
}

void GenParticle1::DeleteThis()
{
    delete this;
}

IOResult GenParticle1::Save(ISave* isave)
{
    ULONG nb = 0;

    CStr ansiPath(m_modelPath.ToCStr());
    int len = static_cast<int>(strlen(ansiPath.data())) + 1;

    isave->BeginChunk(1000);
    isave->Write(&len, sizeof(int), &nb);
    isave->Write(ansiPath.data(), len, &nb);
    isave->EndChunk();

    CStr ansiPrefix(m_modelPrefix.ToCStr());
    int plen = static_cast<int>(strlen(ansiPrefix.data())) + 1;

    isave->BeginChunk(1001);
    isave->Write(&plen, sizeof(int), &nb);
    isave->Write(ansiPrefix.data(), plen, &nb);
    isave->EndChunk();

    return IO_OK;
}

IOResult GenParticle1::Load(ILoad* iload)
{
    IOResult res;
    while ((res = iload->OpenChunk()) == IO_OK) {
        if (iload->CurChunkID() == 1000)
            LoadAnsiChunk(iload, m_modelPath);
        else if (iload->CurChunkID() == 1001)
            LoadAnsiChunk(iload, m_modelPrefix);
        iload->CloseChunk();
    }
    return IO_OK;
}

void* GenParticle1::GetInterface(ULONG id) {
    if (id == WC3P1_MODEL_PATH_IID)   return &m_modelPath;
    if (id == WC3P1_MODEL_PREFIX_IID) return &m_modelPrefix;
    return SimpleParticle::GetInterface(id);
}

void GenParticle1::BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev)
{
    SimpleParticle::BeginEditParams(ip, flags, prev);
    GetWc3Particles1Desc()->BeginEditParams(ip, this, flags, prev);
    wc3particles1_param_blk.SetUserDlgProc(P1_MAP_EMITTER, new Wc3Particles1DlgProc(this));
    wc3particles1_param_blk.SetUserDlgProc(P1_MAP_TIMING,  new Wc3Particles1DlgProc(this));
    wc3particles1_param_blk.SetUserDlgProc(P1_MAP_MODEL,   new Wc3Particles1DlgProc(this));
    wc3particles1_param_blk.SetUserDlgProc(P1_MAP_CONFIG,  new Config1DlgProc(this));
}

void GenParticle1::EndEditParams(IObjParam* ip, ULONG flags, Animatable* next)
{
    SimpleParticle::EndEditParams(ip, flags, next);
    GetWc3Particles1Desc()->EndEditParams(ip, this, flags, next);
}

// ============================================================================
// Wc3Particles1Particle implementation
// ============================================================================

Wc3Particles1Particle::Wc3Particles1Particle()
    : GenParticle1()
{
    GetWc3Particles1Desc()->MakeAutoParamBlocks(this);
    assert(pblock2);
}

Wc3Particles1Particle::~Wc3Particles1Particle() = default;

Class_ID Wc3Particles1Particle::ClassID()
{
    return WC3PARTICLES1_CLASS_ID;
}

#if MAX_PRODUCT_YEAR_NUMBER >= 2022
const MCHAR* Wc3Particles1Particle::GetObjectName(bool localized) const
#else
const MCHAR* Wc3Particles1Particle::GetObjectName()
#endif
{
    return GetString(IDS_P1_OBJECT_NAME);
}

BOOL Wc3Particles1Particle::IsInstanceDependent()
{
    return TRUE;
}

float Wc3Particles1Particle::ParticleSize(TimeValue t, int /*i*/)
{
    float scale = 1.0f;
    Interval forever = FOREVER;
    pblock2->GetValue(P1_PB_SCALE, t, scale, forever);
    return scale;
}

int Wc3Particles1Particle::ParticleCenter(TimeValue /*t*/, int /*i*/)
{
    return 2;   // CENTER
}

MarkerType Wc3Particles1Particle::GetMarkerType()
{
    float scale = 1.0f;
    Interval forever = FOREVER;
    pblock2->GetValue(P1_PB_SCALE, 0, scale, forever);
    theWc3Particles1Draw.scale = scale;
    parts.SetCustomDraw(&theWc3Particles1Draw);
    return static_cast<MarkerType>(0);
}

ReferenceTarget* Wc3Particles1Particle::Clone(RemapDir& remap)
{
    auto* newob = new Wc3Particles1Particle();
    newob->ReplaceReference(0, remap.CloneRef(pblock2));
    newob->m_modelPath = m_modelPath;
    newob->m_modelPrefix = m_modelPrefix;
    newob->mvalid.SetEmpty();
    newob->tvalid = 0;
    newob->valid = FALSE;
    BaseClone(this, newob, remap);
    return newob;
}

Mesh* Wc3Particles1Particle::GetRenderMesh(TimeValue t, INode* inode, View& view,
                                            BOOL& needDelete)
{
    float scale = 1.0f;
    Interval forever = FOREVER;
    pblock2->GetValue(P1_PB_SCALE, t, scale, forever);

    Matrix3 tm = Inverse(inode->GetObjTMAfterWSM(t));
    int ix = 0, nx = 0;

    auto* pm = new Mesh;

    ParticleSys lastparts;
    int TicksPerFrame = GetTicksPerFrame();
    int offtime = t % TicksPerFrame;
    BOOL midframe = (offtime > 0);

    if (midframe) {
        Update(t - offtime, inode);
        ParticleCacheData(&lastparts, &parts);
    }
    Update(t, inode);

    int count = CountLive();

    // 6 verts, 2 faces per particle (radial hexagon)
    constexpr int numVPerPart = 6;
    constexpr int numFPerPart = 2;

    pm->setNumFaces(numFPerPart * count);
    pm->setNumVerts(numVPerPart * count);
    pm->setNumTVerts(numVPerPart * count);
    pm->setNumTVFaces(numFPerPart * count);

    for (int i = 0; i < parts.Count(); i++) {
        if (!parts.Alive(i))
            continue;

        float sz = scale;

        for (int j = 0; j < numVPerPart; j++) {
            float angle = (TWOPI * static_cast<float>(j)) / static_cast<float>(numVPerPart);
            pm->verts[j + ix].x = cosf(angle) * sz;
            pm->verts[j + ix].y = 0.0f;
            pm->verts[j + ix].z = sinf(angle) * sz;

            pm->tVerts[j + ix].x = cosf(angle) * 0.5f + 0.5f;
            pm->tVerts[j + ix].y = sinf(angle) * 0.5f + 0.5f;
            pm->tVerts[j + ix].z = 0.0f;

            pm->verts[j + ix] += parts[i];
            pm->verts[j + ix] = pm->verts[j + ix] * tm;
        }

        pm->faces[nx].setSmGroup(0);
        pm->faces[nx].setVerts(ix, ix + 2, ix + 4);
        pm->faces[nx].setMatID(static_cast<MtlID>(i));
        pm->faces[nx].setEdgeVisFlags(1, 1, 1);
        pm->faces[nx + 1].setSmGroup(0);
        pm->faces[nx + 1].setVerts(ix + 1, ix + 3, ix + 5);
        pm->faces[nx + 1].setMatID(static_cast<MtlID>(i));
        pm->faces[nx + 1].setEdgeVisFlags(1, 1, 1);
        pm->tvFace[nx].setTVerts(ix, ix + 2, ix + 4);
        pm->tvFace[nx + 1].setTVerts(ix + 1, ix + 3, ix + 5);

        ix += numVPerPart;
        nx += numFPerPart;
    }

    if (midframe) {
        ParticleCacheData(&parts, &lastparts);
        tvalid = t - offtime;
    }

    if (Parity(&tm))
        FlipAllMeshFaces(pm);

    mesh.InvalidateGeomCache();
    needDelete = TRUE;
    return pm;
}
