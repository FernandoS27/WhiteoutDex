/**
 * @file Particles.cpp
 * @brief Implementation of the Wc3Particles2 3ds Max particle plugin.
 *
 * Provides the full simulation loop, PB2 descriptor, viewport/render mesh
 * generation, UI dialog callbacks, serialisation, and DLL entry points.
 */

#include "Particles.h"

/// @name Module globals
/// @{
HINSTANCE hInstance = nullptr;
static MCHAR s_stringBuf[256];
constexpr UINT_PTR kCascPollTimerID = 42;

static Wc3Particles2ClassDesc        snowDesc;
static EmitterCreateCallback    emitterCallback;
static Wc3Particles2ParticleDraw     theWc3Particles2Draw;
/// @}

/**
 * @brief ParamBlock2 descriptor for all 46 Wc3Particles2 parameters.
 *
 * Uses @c P_AUTO_CONSTRUCT to let ClassDesc2::MakeAutoParamBlocks() create the
 * block automatically, and @c P_AUTO_UI to wire spinners, checkboxes, radio
 * buttons, and colour swatches to the rollup dialog without manual subclassing.
 */
static ParamBlockDesc2 wc3particles2_param_blk(
    wc3particles2_params,                 // Block ID = 0
    _M("Wc3Particles2Parameters"),        // Internal name
    0,                              // Resource string ID (none)
    &snowDesc,                      // ClassDesc2*
    P_AUTO_CONSTRUCT | P_AUTO_UI | P_MULTIMAP | P_HASCATEGORY,   // Flags

    // --- P_AUTO_CONSTRUCT ref# ---
    0,

    // --- P_MULTIMAP: 7 rollouts (ROLLUP_SAVECAT forces Max to respect categories) ---
    MAP_COUNT,
    MAP_CONFIG,   IDD_ROLLOUT_CONFIG,   IDS_ROLLOUT_CONFIG,   0, ROLLUP_SAVECAT, NULL, ROLLUP_CAT_STANDARD,
    MAP_TEXTURE,  IDD_ROLLOUT_TEXTURE,  IDS_ROLLOUT_TEXTURE,  0, ROLLUP_SAVECAT, NULL, ROLLUP_CAT_STANDARD+1,
    MAP_EMITTER,  IDD_ROLLOUT_EMITTER,  IDS_ROLLOUT_EMITTER,  0, ROLLUP_SAVECAT, NULL, ROLLUP_CAT_STANDARD+2,
    MAP_TIMING,   IDD_ROLLOUT_TIMING,   IDS_ROLLOUT_TIMING,   0, ROLLUP_SAVECAT, NULL, ROLLUP_CAT_STANDARD+3,
    MAP_SIZE,     IDD_ROLLOUT_SIZE,     IDS_ROLLOUT_SIZE,     0, ROLLUP_SAVECAT, NULL, ROLLUP_CAT_STANDARD+4,
    MAP_PARTICLE, IDD_ROLLOUT_PARTICLE, IDS_ROLLOUT_PARTICLE, 0, ROLLUP_SAVECAT, NULL, ROLLUP_CAT_STANDARD+5,
    MAP_OTHER,    IDD_ROLLOUT_OTHER,    IDS_ROLLOUT_OTHER,    0, ROLLUP_SAVECAT, NULL, ROLLUP_CAT_STANDARD+6,

    // [0] PB_COUNT
    PB_COUNT, _M("Count"), TYPE_INT, 0, IDS_PARAM_COUNT,
        p_default,  500,
        p_range,    1, 100000000,
        p_ui,       MAP_EMITTER, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_COUNT, IDC_SPIN_COUNT, 1.0f,
    p_end,

    // [1] PB_SPEED
    PB_SPEED, _M("Speed"), TYPE_FLOAT, P_ANIMATABLE, IDS_PARAM_SPEED,
        p_default,  10.0f,
        p_range,    -1000000000.0f, 1000000000.0f,
        p_ui,       MAP_EMITTER, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_EDIT_SPEED, IDC_SPIN_SPEED, 0.005f,
    p_end,

    // [2] PB_VARIATION
    PB_VARIATION, _M("Variation"), TYPE_FLOAT, P_ANIMATABLE, IDS_PARAM_VARIATION,
        p_default,  0.0f,
        p_range,    0.0f, 1000000000.0f,
        p_ui,       MAP_EMITTER, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_EDIT_VARIATION, IDC_SPIN_VARIATION, 1.0f,
    p_end,

    // [3] PB_LIFE
    PB_LIFE, _M("Life"), TYPE_FLOAT, 0, IDS_PARAM_LIFE,
        p_default,  1.0f,
        p_range,    0.0f, 1000000000.0f,
        p_ui,       MAP_TIMING, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_EDIT_LIFE, IDC_SPIN_LIFE, 0.005f,
    p_end,

    // [4] PB_WIDTH
    PB_WIDTH, _M("Width"), TYPE_FLOAT, P_ANIMATABLE, IDS_WIDTH,
        p_default,  0.0f,
        p_range,    0.0f, 1000000000.0f,
        p_ui,       MAP_SIZE, TYPE_SPINNER, EDITTYPE_UNIVERSE,
                    IDC_EDIT_WIDTH, IDC_SPIN_WIDTH, SPIN_AUTOSCALE,
    p_end,

    // [5] PB_HEIGHT
    PB_HEIGHT, _M("Height"), TYPE_FLOAT, P_ANIMATABLE, IDS_HEIGHT,
        p_default,  0.0f,
        p_range,    0.0f, 1000000000.0f,
        p_ui,       MAP_SIZE, TYPE_SPINNER, EDITTYPE_UNIVERSE,
                    IDC_EDIT_HEIGHT, IDC_SPIN_HEIGHT, SPIN_AUTOSCALE,
    p_end,

    // [6] PB_INITVEL
    PB_INITVEL, _M("EmissionRate"), TYPE_FLOAT, P_ANIMATABLE, IDS_PARAM_INITVEL,
        p_default,  50.0f,
        p_range,    0.0f, 1000000000.0f,
        p_ui,       MAP_TIMING, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_EDIT_INITVEL, IDC_SPIN_INITVEL, 0.1f,
    p_end,

    // [7] PB_ANGLE_Y
    PB_ANGLE_Y, _M("ConeAngle"), TYPE_FLOAT, P_ANIMATABLE, IDS_PARAM_ANGLE_Y,
        p_default,  0.0f,
        p_range,    0.0f, 180.0f,
        p_ui,       MAP_EMITTER, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_EDIT_ANGLE_Y, IDC_SPIN_ANGLE_Y, 0.5f,
    p_end,

    // [8] PB_MIDTIME
    PB_MIDTIME, _M("MidTime"), TYPE_FLOAT, 0, IDS_PARAM_MIDTIME,
        p_default,  0.5f,
        p_range,    0.05f, 0.95f,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_EDIT_MIDTIME, IDC_SPIN_MIDTIME, 0.05f,
    p_end,

    // [9] PB_COLOR_START
    PB_COLOR_START, _M("ColorStart"), TYPE_POINT3, 0, IDS_PARAM_COLOR_START,
        p_default,  Point3(1, 1, 1),
        p_ui,       MAP_PARTICLE, TYPE_COLORSWATCH, IDC_COLOR_START,
    p_end,

    // [10] PB_COLOR_MID
    PB_COLOR_MID, _M("ColorMid"), TYPE_POINT3, 0, IDS_PARAM_COLOR_MID,
        p_default,  Point3(1, 1, 1),
        p_ui,       MAP_PARTICLE, TYPE_COLORSWATCH, IDC_COLOR_MID,
    p_end,

    // [11] PB_COLOR_END
    PB_COLOR_END, _M("ColorEnd"), TYPE_POINT3, 0, IDS_PARAM_COLOR_END,
        p_default,  Point3(1, 1, 1),
        p_ui,       MAP_PARTICLE, TYPE_COLORSWATCH, IDC_COLOR_END,
    p_end,

    // [12] PB_ALPHA_START
    PB_ALPHA_START, _M("AlphaStart"), TYPE_INT, 0, IDS_PARAM_ALPHA_START,
        p_default,  255,
        p_range,    0, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_START_COLOR_R, IDC_SPIN_START_COLOR_R, 1.0f,
    p_end,

    // [13] PB_ALPHA_MID
    PB_ALPHA_MID, _M("AlphaMid"), TYPE_INT, 0, IDS_PARAM_ALPHA_MID,
        p_default,  255,
        p_range,    0, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_START_COLOR_G, IDC_SPIN_START_COLOR_G, 1.0f,
    p_end,

    // [14] PB_ALPHA_END
    PB_ALPHA_END, _M("AlphaEnd"), TYPE_INT, 0, IDS_PARAM_ALPHA_END,
        p_default,  0,
        p_range,    0, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_START_COLOR_B, IDC_SPIN_START_COLOR_B, 1.0f,
    p_end,

    // [15] PB_SCALE_START
    PB_SCALE_START, _M("ScaleStart"), TYPE_FLOAT, 0, IDS_PARAM_SCALE_START,
        p_default,  10.0f,
        p_range,    0.001f, 500.0f,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_EDIT_STARTSIZE, IDC_SPIN_STARTSIZE, 0.1f,
    p_end,

    // [16] PB_SCALE_MID
    PB_SCALE_MID, _M("ScaleMid"), TYPE_FLOAT, 0, IDS_PARAM_SCALE_MID,
        p_default,  10.0f,
        p_range,    0.001f, 500.0f,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_EDIT_MIDSIZE, IDC_SPIN_MIDSIZE, 0.1f,
    p_end,

    // [17] PB_SCALE_END
    PB_SCALE_END, _M("ScaleEnd"), TYPE_FLOAT, 0, IDS_PARAM_SCALE_END,
        p_default,  10.0f,
        p_range,    0.001f, 500.0f,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_EDIT_ENDSIZE, IDC_SPIN_ENDSIZE, 0.1f,
    p_end,

    // [18] PB_HEAD_LIFE_START
    PB_HEAD_LIFE_START, _M("HeadLifeStart"), TYPE_INT, 0, IDS_PARAM_HEAD_LIFE_START,
        p_default,  0,
        p_range,    0, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_START_ALPHA, IDC_SPIN_START_ALPHA, 1.0f,
    p_end,

    // [19] PB_HEAD_LIFE_REPEAT
    PB_HEAD_LIFE_REPEAT, _M("HeadLifeRepeat"), TYPE_INT, 0, IDS_PARAM_HEAD_LIFE_REPEAT,
        p_default,  1,
        p_range,    1, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_MID_ALPHA, IDC_SPIN_MID_ALPHA, 1.0f,
    p_end,

    // [20] PB_HEAD_LIFE_END
    PB_HEAD_LIFE_END, _M("HeadLifeEnd"), TYPE_INT, 0, IDS_PARAM_HEAD_LIFE_END,
        p_default,  0,
        p_range,    0, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_END_ALPHA, IDC_SPIN_END_ALPHA, 1.0f,
    p_end,

    // [21] PB_HEAD_DECAY_START
    PB_HEAD_DECAY_START, _M("HeadDecayStart"), TYPE_INT, 0, IDS_PARAM_HEAD_DECAY_START,
        p_default,  0,
        p_range,    0, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_START_SCALE_X, IDC_SPIN_START_SCALE_X, 1.0f,
    p_end,

    // [22] PB_HEAD_DECAY_REPEAT
    PB_HEAD_DECAY_REPEAT, _M("HeadDecayRepeat"), TYPE_INT, 0, IDS_PARAM_HEAD_DECAY_REPEAT,
        p_default,  1,
        p_range,    1, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_END_SCALE_X, IDC_SPIN_END_SCALE_X, 1.0f,
    p_end,

    // [23] PB_HEAD_DECAY_END
    PB_HEAD_DECAY_END, _M("HeadDecayEnd"), TYPE_INT, 0, IDS_PARAM_HEAD_DECAY_END,
        p_default,  0,
        p_range,    0, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_START_SCALE_Y, IDC_SPIN_START_SCALE_Y, 1.0f,
    p_end,

    // [24] PB_TAIL_LEN
    PB_TAIL_LEN, _M("TailLength"), TYPE_FLOAT, 0, IDS_PARAM_TAIL_LEN,
        p_default,  1.0f,
        p_range,    0.0f, 10.0f,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_EDIT_TAILLEN, IDC_SPIN_TAILLEN, 0.05f,
    p_end,

    // [25] PB_TYPE
    PB_TYPE, _M("ParticleType"), TYPE_INT, 0, IDS_PARAM_TYPE,
        p_default,  0,
        p_ui,       MAP_PARTICLE, TYPE_RADIO, 3, IDC_RADIO_TYPE_HEAD, IDC_RADIO_TYPE_TAIL, IDC_RADIO_TYPE_BOTH,
    p_end,

    // [26] PB_ROWS
    PB_ROWS, _M("TextureRows"), TYPE_INT, 0, 0,
        p_default,  1,
        p_range,    1, 16,
        p_ui,       MAP_TEXTURE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_ROWS, IDC_SPIN_ROWS, 1.0f,
    p_end,

    // [27] PB_COLS
    PB_COLS, _M("TextureCols"), TYPE_INT, 0, 0,
        p_default,  1,
        p_range,    1, 16,
        p_ui,       MAP_TEXTURE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_COLS, IDC_SPIN_COLS, 1.0f,
    p_end,

    // [28] PB_TAIL_LIFE_START
    PB_TAIL_LIFE_START, _M("TailLifeStart"), TYPE_INT, 0, IDS_PARAM_TAIL_LIFE_START,
        p_default,  0,
        p_range,    0, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_END_COLOR_R, IDC_SPIN_END_COLOR_R, 1.0f,
    p_end,

    // [29] PB_TAIL_LIFE_REPEAT
    PB_TAIL_LIFE_REPEAT, _M("TailLifeRepeat"), TYPE_INT, 0, IDS_PARAM_TAIL_LIFE_REPEAT,
        p_default,  1,
        p_range,    1, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_END_COLOR_G, IDC_SPIN_END_COLOR_G, 1.0f,
    p_end,

    // [30] PB_TAIL_LIFE_END
    PB_TAIL_LIFE_END, _M("TailLifeEnd"), TYPE_INT, 0, IDS_PARAM_TAIL_LIFE_END,
        p_default,  0,
        p_range,    0, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_MID_COLOR_R, IDC_SPIN_MID_COLOR_R, 1.0f,
    p_end,

    // [31] PB_TAIL_DECAY_START
    PB_TAIL_DECAY_START, _M("TailDecayStart"), TYPE_INT, 0, IDS_PARAM_TAIL_DECAY_START,
        p_default,  0,
        p_range,    0, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_MID_COLOR_G, IDC_SPIN_MID_COLOR_G, 1.0f,
    p_end,

    // [32] PB_TAIL_DECAY_REPEAT
    PB_TAIL_DECAY_REPEAT, _M("TailDecayRepeat"), TYPE_INT, 0, IDS_PARAM_TAIL_DECAY_REPEAT,
        p_default,  1,
        p_range,    1, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_END_COLOR_B, IDC_SPIN_END_COLOR_B, 1.0f,
    p_end,

    // [33] PB_TAIL_DECAY_END
    PB_TAIL_DECAY_END, _M("TailDecayEnd"), TYPE_INT, 0, IDS_PARAM_TAIL_DECAY_END,
        p_default,  0,
        p_range,    0, 255,
        p_ui,       MAP_PARTICLE, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_MID_COLOR_B, IDC_SPIN_MID_COLOR_B, 1.0f,
    p_end,

    // [34] PB_SQUIRT
    PB_SQUIRT, _M("Squirt"), TYPE_BOOL, 0, IDS_PARAM_SQUIRT,
        p_default,  FALSE,
        p_ui,       MAP_TIMING, TYPE_SINGLECHEKBOX, IDC_CHECKBOX_SQUIRT,
    p_end,

    // [35] PB_BLEND — manually driven by IDC_COMBO_BLEND combobox
    PB_BLEND, _M("BlendMode"), TYPE_INT, 0, 0,
        p_default,  1,
    p_end,

    // [36] PB_GRAVITY
    PB_GRAVITY, _M("Gravity"), TYPE_FLOAT, P_ANIMATABLE, IDS_PARAM_GRAVITY,
        p_default,  0.0f,
        p_range,    0.0f, 100.0f,
        p_ui,       MAP_EMITTER, TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_EDIT_GRAVITY, IDC_SPIN_GRAVITY, 0.1f,
    p_end,

    // [37] PB_SORT
    PB_SORT, _M("SortPrimitives"), TYPE_BOOL, 0, 0,
        p_default,  FALSE,
        p_ui,       MAP_TEXTURE, TYPE_SINGLECHEKBOX, IDC_CHECKBOX_SORT,
    p_end,

    // [38] PB_LINE_EMIT
    PB_LINE_EMIT, _M("LineEmitter"), TYPE_BOOL, 0, 0,
        p_default,  FALSE,
        p_ui,       MAP_SIZE, TYPE_SINGLECHEKBOX, IDC_CHECKBOX_LINE_EMITTER,
    p_end,

    // [39] PB_UNSHADED
    PB_UNSHADED, _M("Unshaded"), TYPE_BOOL, 0, 0,
        p_default,  FALSE,
        p_ui,       MAP_TEXTURE, TYPE_SINGLECHEKBOX, IDC_CHECKBOX_UNSHADED,
    p_end,

    // [40] PB_LATITUDE — deprecated, kept for file compatibility. Use PB_ANGLE_Y instead.
    PB_LATITUDE, _M("Latitude"), TYPE_FLOAT, P_INVISIBLE, 0,
        p_default,  0.0f,
    p_end,

    // [41] PB_PRIORITY
    PB_PRIORITY, _M("PriorityPlane"), TYPE_INT, 0, 0,
        p_default,  0,
        p_range,    -100, 100,
        p_ui,       MAP_OTHER, TYPE_SPINNER, EDITTYPE_INT,
                    IDC_EDIT_PRIORITY, IDC_SPIN_PRIORITY, 1.0f,
    p_end,

    // [42] PB_UNFOGGED
    PB_UNFOGGED, _M("Unfogged"), TYPE_BOOL, 0, 0,
        p_default,  FALSE,
        p_ui,       MAP_TEXTURE, TYPE_SINGLECHEKBOX, IDC_CHECKBOX_UNFOGGED,
    p_end,

    // [43] PB_MODELSPACE
    PB_MODELSPACE, _M("ModelSpace"), TYPE_BOOL, 0, 0,
        p_default,  FALSE,
        p_ui,       MAP_OTHER, TYPE_SINGLECHEKBOX, IDC_CHECKBOX_MODELSPACE,
    p_end,

    // [44] PB_XYQUAD
    PB_XYQUAD, _M("XYQuad"), TYPE_BOOL, 0, 0,
        p_default,  FALSE,
        p_ui,       MAP_OTHER, TYPE_SINGLECHEKBOX, IDC_CHECKBOX_XYQUAD,
    p_end,

    // [45] PB_REPLACEABLE_ID
    PB_REPLACEABLE_ID, _M("ReplaceableId"), TYPE_INT, 0, 0,
        p_default,  0,
        p_range,    0, 2,
    p_end,

    // [46] PB_LONGITUDE — internal, derived from line-emitter state. Not user-editable.
    PB_LONGITUDE, _M("Longitude"), TYPE_FLOAT, P_INVISIBLE, 0,
        p_default,  180.0f,
    p_end,

    p_end   // Final terminator
);

/// @name Utility functions
/// @{

const MCHAR* GetString(UINT id)
{
    if (!hInstance)
        return nullptr;
    if (!::LoadString(hInstance, id, s_stringBuf, _countof(s_stringBuf)))
        return nullptr;
    return s_stringBuf;
}

ClassDesc2* GetWc3Particles2Desc()
{
    return &snowDesc;
}

/// @}

/// @name DLL entry points
/// @{

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
    return GetString(IDS_LIB_DESCRIPTION);
}

extern "C" __declspec(dllexport) int LibNumberClasses()
{
    return 1;
}

extern "C" __declspec(dllexport) ClassDesc* LibClassDesc(int i)
{
    return (i == 0) ? GetWc3Particles2Desc() : nullptr;
}

extern "C" __declspec(dllexport) ULONG LibVersion()
{
    return VERSION_3DSMAX;
}

extern "C" __declspec(dllexport) ULONG CanAutoDefer()
{
    return 1;
}

/// @}

/// @name Wc3Particles2ClassDesc implementation
/// @{

int Wc3Particles2ClassDesc::IsPublic()
{
    return 1;
}

void* Wc3Particles2ClassDesc::Create(BOOL /*loading*/)
{
    return new Wc3Particles2Particle();
}

const TCHAR* Wc3Particles2ClassDesc::ClassName()
{
    return GetString(IDS_CLASS_NAME);
}

#if MAX_PRODUCT_YEAR_NUMBER >= 2022
const MCHAR* Wc3Particles2ClassDesc::NonLocalizedClassName()
{
    return GetString(IDS_CLASS_NAME);
}
#endif

SClass_ID Wc3Particles2ClassDesc::SuperClassID()
{
    return GEOMOBJECT_CLASS_ID;
}

Class_ID Wc3Particles2ClassDesc::ClassID()
{
    return WC3PARTICLES2_CLASS_ID;
}

const TCHAR* Wc3Particles2ClassDesc::Category()
{
    return GetString(IDS_CATEGORY);
}

const TCHAR* Wc3Particles2ClassDesc::InternalName()
{
    return _M("Wc3Particles2Emitter");
}

HINSTANCE Wc3Particles2ClassDesc::HInstance()
{
    return hInstance;
}

/// @}

/// @name EmitterCreateCallback implementation
/// @{

int EmitterCreateCallback::proc(ViewExp* vpt, int msg, int point, int flags,
                                IPoint2 m, Matrix3& mat)
{
    if (msg == MOUSE_FREEMOVE) {
        vpt->SnapPreview(m, m, nullptr, SNAP_IN_PLANE);
    }
    if (msg != MOUSE_POINT && msg != MOUSE_MOVE) {
        return (msg == MOUSE_ABORT) ? CREATE_ABORT : CREATE_CONTINUE;
    }

    switch (point) {
    case 0: {
        sp0 = m;
        p0 = vpt->SnapPoint(m, m, nullptr, SNAP_IN_PLANE);
        mat.SetTrans(p0);
        rain->pblock2->SetValue(PB_WIDTH, 0, 0.0f);
        rain->pblock2->SetValue(PB_HEIGHT, 0, 0.0f);
        break;
    }
    case 1: {
        mat.IdentityMatrix();
        sp1 = m;
        p1 = vpt->SnapPoint(m, m, nullptr, SNAP_IN_PLANE);
        Point3 center = (p0 + p1) / 2.0f;
        mat.SetTrans(center);
        float width  = std::abs(p1.x - p0.x);
        float height = std::abs(p1.y - p0.y);
        rain->pblock2->SetValue(PB_WIDTH, 0, width);
        rain->pblock2->SetValue(PB_HEIGHT, 0, height);

        if (msg == MOUSE_POINT) {
            if (Length(m - sp0) >= 3 && Length(p1 - p0) >= 0.1f)
                return CREATE_STOP;
            return CREATE_ABORT;
        }
        break;
    }
    }
    return CREATE_CONTINUE;
}

/// @}

/// @name Wc3Particles2DlgProc implementation
/// @{

void Wc3Particles2DlgProc::SetupMaxRate(HWND hWnd, IParamBlock2* pb, TimeValue t)
{
    float life = 0.0f;
    float emission = 0.0f;
    Interval forever = FOREVER;

    pb->GetValue(PB_LIFE, t, life, forever);
    pb->GetValue(PB_INITVEL, t, emission, forever);

    /// Auto-compute count = emission_rate * life, clamped to PARAM_COUNT_MAX.
    int count = 0;
    if (life > 0.0f && emission > 0.0f)
        count = std::min(static_cast<int>(emission * life), PARAM_COUNT_MAX);
    pb->SetValue(PB_COUNT, t, count);

    /// Update the (read-only) count spinner to reflect the new value.
    ISpinnerControl* spin = GetISpinner(GetDlgItem(hWnd, IDC_SPIN_COUNT));
    if (spin) {
        spin->SetValue(count, FALSE);
        ReleaseISpinner(spin);
    }

    float maxRate = 0.0f;
    if (life > 0.0f && count > 0)
        maxRate = static_cast<float>(TICKS_PER_SEC) * count / life;

    HWND hCtrl = GetDlgItem(hWnd, IDC_STATIC_MAXRATE);
    if (hCtrl) {
        TCHAR str[64];
        _stprintf_s(str, _countof(str), _T("%.1f"), maxRate);
        SetWindowText(hCtrl, str);
    }
}

void Wc3Particles2DlgProc::BrowseForMdlFile(HWND hWnd)
{
    TCHAR fileBuf[MAX_PATH] = _T("");
    OPENFILENAME ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hWnd;
    // Multi-format filter: BLP/DDS for Wc3 native, TGA for legacy MDX,
    // PNG/JPG/BMP for working files. Default to "all texture files" so the
    // dialog isn't artificially restrictive.
    ofn.lpstrFilter  = _T("Texture Files\0*.blp;*.dds;*.tga;*.png;*.jpg;*.jpeg;*.bmp\0")
                       _T("BLP (Wc3)\0*.blp\0")
                       _T("DDS\0*.dds\0")
                       _T("TGA\0*.tga\0")
                       _T("PNG\0*.png\0")
                       _T("JPEG\0*.jpg;*.jpeg\0")
                       _T("BMP\0*.bmp\0")
                       _T("All Files\0*.*\0");
    ofn.lpstrFile    = fileBuf;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt  = _T("blp");

    if (GetOpenFileName(&ofn)) {
    /// Try UNC path conversion so mapped drives are stored as network paths.
        TCHAR uncBuf[1024];
        DWORD uncSize = sizeof(uncBuf);
        if (WNetGetUniversalName(fileBuf, UNIVERSAL_NAME_INFO_LEVEL,
                                 uncBuf, &uncSize) == NO_ERROR) {
            auto* uni = reinterpret_cast<UNIVERSAL_NAME_INFO*>(uncBuf);
            _tcscpy_s(fileBuf, uni->lpUniversalName);
        }
        po->m_particlePath = fileBuf;

        ICustEdit* ce = GetICustEdit(GetDlgItem(hWnd, IDC_CUSTOMEDIT_PATH));
        if (ce) {
            ce->SetText(fileBuf);
            ReleaseICustEdit(ce);
        }
    }
}

static const TexCategoryEntry s_texCategories[] = {
    { L"No Replaceable", 0 },
    { L"TeamColor",      1 },
    { L"TeamGlow",       2 },
};

void BuildTexCategoryList(std::vector<TexCategoryEntry>& list)
{
    list.assign(std::begin(s_texCategories), std::end(s_texCategories));
}

/// @brief Controller type names for the controller-type comboboxes.
static const MCHAR* s_ctrlTypeNames[] = { _M("None"), _M("Linear"), _M("Bezier"), _M("Hermite") };
static const int    s_ctrlTypeCount = 4;

/// @brief Detect which controller type is assigned to a PB2 parameter.
static int DetectControllerType(IParamBlock2* pb, ParamID pid)
{
    int tabIndex = pb->GetDesc()->IDtoIndex(pid);
    Control* ctrl = pb->GetControllerByIndex(tabIndex);
    if (!ctrl) return 0; // None
    Class_ID cid = ctrl->ClassID();
    if (cid == Class_ID(LININTERP_FLOAT_CLASS_ID, 0)) return 1; // Linear
    if (cid == Class_ID(HYBRIDINTERP_FLOAT_CLASS_ID, 0)) return 2; // Bezier
    if (cid == Class_ID(TCBINTERP_FLOAT_CLASS_ID, 0)) return 3; // Hermite
    return 2; // Default to bezier for unknown
}

/// @brief Populate a controller-type combobox and select the current type.
static void SetupCtrlCombo(HWND hWnd, int comboID, IParamBlock2* pb, ParamID pid)
{
    HWND hCombo = GetDlgItem(hWnd, comboID);
    if (!hCombo) return;
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < s_ctrlTypeCount; i++)
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)s_ctrlTypeNames[i]);
    SendMessage(hCombo, CB_SETCURSEL, DetectControllerType(pb, pid), 0);
}

/// @brief Switch the controller type for a PB2 float parameter.
static void SwitchControllerType(IParamBlock2* pb, ParamID pid, int type)
{
    int tabIndex = pb->GetDesc()->IDtoIndex(pid);
    Control* newCtrl = nullptr;
    switch (type) {
    case 0: // None — remove controller
        pb->SetControllerByIndex(tabIndex, 0, nullptr);
        return;
    case 1: // Linear
        newCtrl = static_cast<Control*>(
            CreateInstance(CTRL_FLOAT_CLASS_ID, Class_ID(LININTERP_FLOAT_CLASS_ID, 0)));
        break;
    case 2: // Bezier
        newCtrl = NewDefaultFloatController();
        break;
    case 3: // Hermite (TCB)
        newCtrl = static_cast<Control*>(
            CreateInstance(CTRL_FLOAT_CLASS_ID, Class_ID(TCBINTERP_FLOAT_CLASS_ID, 0)));
        break;
    }
    if (newCtrl)
        pb->SetControllerByIndex(tabIndex, 0, newCtrl);
}

/// Mapping of controller combobox IDs to ParamBlock parameter IDs.
struct CtrlComboMapping { int comboID; ParamID paramID; };
static const CtrlComboMapping s_ctrlMappings[] = {
    { IDC_CTRL_SPEED,     PB_SPEED },
    { IDC_CTRL_VARIATION, PB_VARIATION },
    { IDC_CTRL_ANGLE_Y,   PB_ANGLE_Y },
    { IDC_CTRL_GRAVITY,   PB_GRAVITY },
    { IDC_CTRL_INITVEL,   PB_INITVEL },
    { IDC_CTRL_WIDTH,     PB_WIDTH },
    { IDC_CTRL_HEIGHT,    PB_HEIGHT },
};
static constexpr int s_ctrlMappingCount = static_cast<int>(std::size(s_ctrlMappings));

INT_PTR Wc3Particles2DlgProc::DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                                  UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
    {
        IParamBlock2* pb = map->GetParamBlock();
        SetupMaxRate(hWnd, pb, t);

        /// Disable the count edit and spinner — particle count is auto-computed.
        {
            ISpinnerControl* spin = GetISpinner(GetDlgItem(hWnd, IDC_SPIN_COUNT));
            if (spin) {
                spin->Disable();
                ReleaseISpinner(spin);
            }
            ICustEdit* countEdit = GetICustEdit(GetDlgItem(hWnd, IDC_EDIT_COUNT));
            if (countEdit) {
                countEdit->Disable();
                ReleaseICustEdit(countEdit);
            }
        }

        /// Set particle path edit control.
        ICustEdit* ce = GetICustEdit(GetDlgItem(hWnd, IDC_CUSTOMEDIT_PATH));
        if (ce) {
            ce->SetText(po->m_particlePath.data());
            ReleaseICustEdit(ce);
        }

        /// Set texture path prefix edit control.
        {
            ICustEdit* cePrefix = GetICustEdit(GetDlgItem(hWnd, IDC_EDIT_PATH_PREFIX));
            if (cePrefix) {
                if (po->m_texturePrefix.isNull() || po->m_texturePrefix.length() == 0)
                    po->m_texturePrefix = _M("Textures\\");
                cePrefix->SetText(po->m_texturePrefix.data());
                ReleaseICustEdit(cePrefix);
            }
        }

        /// Build and populate blend mode combo.
        {
            HWND hBlend = GetDlgItem(hWnd, IDC_COMBO_BLEND);
            if (hBlend) {
                SendMessage(hBlend, CB_RESETCONTENT, 0, 0);
                SendMessage(hBlend, CB_ADDSTRING, 0, (LPARAM)_M("Blend"));
                SendMessage(hBlend, CB_ADDSTRING, 0, (LPARAM)_M("Additive"));
                SendMessage(hBlend, CB_ADDSTRING, 0, (LPARAM)_M("Modulate"));
                SendMessage(hBlend, CB_ADDSTRING, 0, (LPARAM)_M("Modulate 2X"));
                SendMessage(hBlend, CB_ADDSTRING, 0, (LPARAM)_M("Alpha Key"));
                int blendVal = 0;
                Interval ivb(TIME_NegInfinity, TIME_PosInfinity);
                pb->GetValue(PB_BLEND, 0, blendVal, ivb);
                SendMessage(hBlend, CB_SETCURSEL, blendVal, 0);
            }
        }

        /// Build and populate replaceable texture category combo.
        std::vector<TexCategoryEntry> categories;
        HWND hCombo = GetDlgItem(hWnd, IDC_COMBO_TEXTURE_CAT);
        if (hCombo) {
            BuildTexCategoryList(categories);
            int curval = 0;
            Interval forever = FOREVER;
            pb->GetValue(PB_REPLACEABLE_ID, 0, curval, forever);

            SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
            UINT select = 0;
            for (const auto& cat : categories) {
                auto n = SendMessage(hCombo, CB_ADDSTRING, 0,
                                     reinterpret_cast<LPARAM>(cat.name.c_str()));
                SendMessage(hCombo, CB_SETITEMDATA, n, cat.index);
                if (curval == cat.index)
                    select = static_cast<UINT>(n);
            }
            SendMessage(hCombo, CB_SETCURSEL, select, 0);
        }

        /// Populate controller-type comboboxes (only the ones present in this dialog).
        for (int m = 0; m < s_ctrlMappingCount; m++)
            SetupCtrlCombo(hWnd, s_ctrlMappings[m].comboID, pb, s_ctrlMappings[m].paramID);

        return TRUE;
    }

    case WM_COMMAND:
    {
        int hiWord = HIWORD(wParam);
        if (hiWord == BN_CLICKED || hiWord == 0) {
            if (LOWORD(wParam) == IDC_BUTTON_BROWSE || LOWORD(wParam) == IDC_BUTTON_IMPORT_TEX)
                BrowseForMdlFile(hWnd);
            else if (LOWORD(wParam) == IDC_BUTTON_BROWSE_CASC) {
                SetTimer(hWnd, kCascPollTimerID, 500, NULL);
                ExecuteMAXScriptScript(
                    _M("global __wc3p2_texResult = undefined\n"
                       "fn __wc3p2CB s e = (::__wc3p2_texResult = e.ArchivePath)\n"
                       "struct __Wc3p2R (texture=\"\", path=\"\")\n"
                       "global __wc3p2_recv = __Wc3p2R()\n"
                       "::WhiteoutDexTexBrowserTarget = #(::__wc3p2_recv, \"particle_texture\")\n"
                       "::WhiteoutDexTexBrowserInitCategory = 4\n"
                       "::WhiteoutDexTexBrowser.show()\n"
                       "if ::WhiteoutDexTexBrowser.form != undefined do "
                       "dotNet.addEventHandler ::WhiteoutDexTexBrowser.form \"TextureApply\" __wc3p2CB\n"),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
                    MAXScript::ScriptSource::NonEmbedded,
#endif
                    FALSE);
            }
            return TRUE;
        }
        if (hiWord == CBN_SELCHANGE) {
            if (LOWORD(wParam) == IDC_COMBO_TEXTURE_CAT) {
                auto sel = SendMessage(reinterpret_cast<HWND>(lParam),
                                       CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    auto idx = SendMessage(reinterpret_cast<HWND>(lParam),
                                           CB_GETITEMDATA, sel, 0);
                    if (idx != CB_ERR)
                        map->GetParamBlock()->SetValue(PB_REPLACEABLE_ID, 0,
                                                       static_cast<int>(idx));
                }
            }
            else if (LOWORD(wParam) == IDC_COMBO_BLEND) {
                auto sel = SendMessage(reinterpret_cast<HWND>(lParam),
                                       CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR)
                    map->GetParamBlock()->SetValue(PB_BLEND, 0, static_cast<int>(sel));
            }
            else {
                // Check controller-type comboboxes
                for (int m = 0; m < s_ctrlMappingCount; m++) {
                    if (LOWORD(wParam) == s_ctrlMappings[m].comboID) {
                        auto sel = SendMessage(reinterpret_cast<HWND>(lParam),
                                               CB_GETCURSEL, 0, 0);
                        if (sel != CB_ERR)
                            SwitchControllerType(map->GetParamBlock(),
                                                 s_ctrlMappings[m].paramID,
                                                 static_cast<int>(sel));
                        break;
                    }
                }
            }
            return TRUE;
        }
        break;
    }

    case CC_SPINNER_CHANGE:
    {
        int spinID = LOWORD(wParam);
        if (spinID == IDC_SPIN_LIFE || spinID == IDC_SPIN_INITVEL)
            SetupMaxRate(hWnd, map->GetParamBlock(), t);
        break;
    }

    case WM_CUSTEDIT_ENTER:
    {
        if (LOWORD(wParam) == IDC_CUSTOMEDIT_PATH) {
            MCHAR buf[MAX_PATH] = {};
            ICustEdit* ce = GetICustEdit(reinterpret_cast<HWND>(lParam));
            if (ce) {
                ce->GetText(buf, MAX_PATH);
                po->m_particlePath = buf;
                ReleaseICustEdit(ce);
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_EDIT_PATH_PREFIX) {
            MCHAR buf[MAX_PATH] = {};
            ICustEdit* ce = GetICustEdit(reinterpret_cast<HWND>(lParam));
            if (ce) {
                ce->GetText(buf, MAX_PATH);
                po->m_texturePrefix = buf;
                ReleaseICustEdit(ce);
            }
            return TRUE;
        }
        break;
    }

    case WM_TIMER:
    {
        if (wParam == kCascPollTimerID) {
            // Poll archive path from our custom TextureApply handler
            FPValue result;
            BOOL ok = ExecuteMAXScriptScript(
                _M("if ::__wc3p2_texResult != undefined do ("
                   "local r = ::__wc3p2_texResult;"
                   "::__wc3p2_texResult = undefined;"
                   "r)"),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
                MAXScript::ScriptSource::NonEmbedded,
#endif
                TRUE, &result);
            if (ok && result.type == TYPE_STRING && result.s != nullptr && result.s[0] != 0) {
                // result.s = archive path, e.g. "Textures/Fireball.blp"
                // Extract just the filename for m_particlePath
                std::wstring ws(result.s);
                // Normalize forward slashes to backslashes
                for (auto& ch : ws)
                    if (ch == L'/') ch = L'\\';
                // Find last backslash to split dir/filename
                auto lastSlash = ws.rfind(L'\\');
                MSTR filename = (lastSlash != std::wstring::npos)
                    ? MSTR(ws.c_str() + lastSlash + 1)
                    : MSTR(ws.c_str());
                MSTR dirPart;
                if (lastSlash != std::wstring::npos) {
                    dirPart = MSTR(ws.substr(0, lastSlash + 1).c_str());
                }

                po->m_particlePath = filename;
                ICustEdit* ce = GetICustEdit(GetDlgItem(hWnd, IDC_CUSTOMEDIT_PATH));
                if (ce) {
                    ce->SetText(filename.data());
                    ReleaseICustEdit(ce);
                }

                // Update prefix from _ntbAssignToSlot (AutoPrefixPath)
                // or from the directory part of the archive path
                if (dirPart.length() > 0) {
                    po->m_texturePrefix = dirPart;
                    ICustEdit* ceP = GetICustEdit(GetDlgItem(hWnd, IDC_EDIT_PATH_PREFIX));
                    if (ceP) {
                        ceP->SetText(dirPart.data());
                        ReleaseICustEdit(ceP);
                    }
                }

                // Timer keeps running for subsequent selections;
                // cleaned up in WM_DESTROY when rollup closes.
            }
        }
        return TRUE;
    }

    case WM_DESTROY:
        KillTimer(hWnd, kCascPollTimerID);
        break;
    }
    return FALSE;
}

/// @}

/// @name ConfigDlgProc implementation (Import/Export)
/// @{

/// Helper: write a key=value line to file
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

/// Simple INI reader: loads all key=value pairs from [Wc3Particles2] section
static std::map<std::string, std::string> IniReadSection(const char* filename) {
    std::map<std::string, std::string> result;
    FILE* f = nullptr;
    fopen_s(&f, filename, "r");
    if (!f) return result;
    char line[4096];
    bool inSection = false;
    while (fgets(line, sizeof(line), f)) {
        // Trim newline
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (line[0] == '[') {
            inSection = (strncmp(line, "[Wc3Particles2]", 15) == 0);
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

/// @brief Export an animatable float parameter, including keyframes if present.
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

/// @brief Import an animatable float parameter, restoring keyframes if present.
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

        // Parse "0f 10|30f 20|60f 5"
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

/// Convert MCHAR path to narrow string for file I/O
static std::string NarrowPath(const MCHAR* widePath) {
    int len = WideCharToMultiByte(CP_UTF8, 0, widePath, -1, nullptr, 0, nullptr, nullptr);
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, widePath, -1, &result[0], len, nullptr, nullptr);
    if (!result.empty() && result.back() == 0) result.pop_back();
    return result;
}

/// Convert a UTF-8 narrow string to a wide MSTR
static MSTR WidenPath(const std::string& narrow) {
    if (narrow.empty()) return MSTR();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, wbuf.data(), wlen);
    return MSTR(wbuf.data());
}

/// Read an ANSI-encoded chunk from ILoad into an MSTR
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

void ConfigDlgProc::ExportConfig(HWND hWnd, IParamBlock2* pb, TimeValue t)
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
    ofn.lpstrTitle   = _M("Export Particle Configuration");

    if (!GetSaveFileName(&ofn)) return;

    std::string path = NarrowPath(szFile);
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "w");
    if (!f) { MessageBox(hWnd, _M("Could not create file."), _M("Export Error"), MB_OK); return; }

    fprintf(f, "[Wc3Particles2]\n");

    Interval iv(TIME_NegInfinity, TIME_PosInfinity);
    int iVal; float fVal; Point3 cVal;

    // Emitter
    pb->GetValue(PB_COUNT, t, iVal, iv); IniWriteInt(f, "Count", iVal);
    ExportAnimFloat(f, "Speed",      "SpeedUseAnim",      pb, PB_SPEED, t);
    ExportAnimFloat(f, "Variation",   "VariationUseAnim",  pb, PB_VARIATION, t);
    ExportAnimFloat(f, "ConeAngle",   "ConeAngleUseAnim", pb, PB_ANGLE_Y, t);
    ExportAnimFloat(f, "Gravity",     "GravityUseAnim",   pb, PB_GRAVITY, t);

    // Timing
    pb->GetValue(PB_LIFE, t, fVal, iv); IniWriteFloat(f, "Life", fVal);
    ExportAnimFloat(f, "EmissionRate", "EmissionRateUseAnim", pb, PB_INITVEL, t);
    pb->GetValue(PB_SQUIRT, t, iVal, iv); IniWriteInt(f, "Squirt", iVal);

    // Size
    ExportAnimFloat(f, "Width",  "WidthUseAnim",  pb, PB_WIDTH, t);
    ExportAnimFloat(f, "Length", "LengthUseAnim", pb, PB_HEIGHT, t);
    pb->GetValue(PB_LINE_EMIT, t, iVal, iv); IniWriteInt(f, "LineEmitter", iVal);

    // Texture
    pb->GetValue(PB_BLEND,     t, iVal, iv); IniWriteInt(f, "BlendMode", iVal);
    pb->GetValue(PB_ROWS,      t, iVal, iv); IniWriteInt(f, "Rows", iVal);
    pb->GetValue(PB_COLS,      t, iVal, iv); IniWriteInt(f, "Cols", iVal);
    pb->GetValue(PB_SORT,      t, iVal, iv); IniWriteInt(f, "SortZ", iVal);
    pb->GetValue(PB_UNSHADED,  t, iVal, iv); IniWriteInt(f, "Unshaded", iVal);
    pb->GetValue(PB_UNFOGGED,  t, iVal, iv); IniWriteInt(f, "Unfogged", iVal);
    pb->GetValue(PB_REPLACEABLE_ID, t, iVal, iv); IniWriteInt(f, "ReplaceableId", iVal);
    {
        std::string narrowPfx = NarrowPath(po->m_texturePrefix.data());
        IniWrite(f, "PreFix", narrowPfx.c_str());
        std::string narrowTex = NarrowPath(po->m_particlePath.data());
        IniWrite(f, "File", narrowTex.c_str());
    }

    // Particle
    pb->GetValue(PB_TYPE,      t, iVal, iv); IniWriteInt(f, "ParticleType", iVal);
    pb->GetValue(PB_TAIL_LEN,  t, fVal, iv); IniWriteFloat(f, "TailLength", fVal);
    pb->GetValue(PB_MIDTIME,   t, fVal, iv); IniWriteFloat(f, "MidTime", fVal);

    // Colors
    pb->GetValue(PB_COLOR_START, t, cVal, iv);
    IniWriteFloat(f, "ColorStart_R", cVal.x); IniWriteFloat(f, "ColorStart_G", cVal.y); IniWriteFloat(f, "ColorStart_B", cVal.z);
    pb->GetValue(PB_COLOR_MID, t, cVal, iv);
    IniWriteFloat(f, "ColorMid_R", cVal.x); IniWriteFloat(f, "ColorMid_G", cVal.y); IniWriteFloat(f, "ColorMid_B", cVal.z);
    pb->GetValue(PB_COLOR_END, t, cVal, iv);
    IniWriteFloat(f, "ColorEnd_R", cVal.x); IniWriteFloat(f, "ColorEnd_G", cVal.y); IniWriteFloat(f, "ColorEnd_B", cVal.z);

    // Alpha, Scale
    pb->GetValue(PB_ALPHA_START, t, iVal, iv); IniWriteInt(f, "AlphaStart", iVal);
    pb->GetValue(PB_ALPHA_MID,   t, iVal, iv); IniWriteInt(f, "AlphaMid", iVal);
    pb->GetValue(PB_ALPHA_END,   t, iVal, iv); IniWriteInt(f, "AlphaEnd", iVal);
    pb->GetValue(PB_SCALE_START, t, fVal, iv); IniWriteFloat(f, "ScaleStart", fVal);
    pb->GetValue(PB_SCALE_MID,   t, fVal, iv); IniWriteFloat(f, "ScaleMid", fVal);
    pb->GetValue(PB_SCALE_END,   t, fVal, iv); IniWriteFloat(f, "ScaleEnd", fVal);

    // Head/Tail UV Anim
    pb->GetValue(PB_HEAD_LIFE_START,  t, iVal, iv); IniWriteInt(f, "HeadLifeStart", iVal);
    pb->GetValue(PB_HEAD_LIFE_END,    t, iVal, iv); IniWriteInt(f, "HeadLifeEnd", iVal);
    pb->GetValue(PB_HEAD_LIFE_REPEAT, t, iVal, iv); IniWriteInt(f, "HeadLifeRepeat", iVal);
    pb->GetValue(PB_HEAD_DECAY_START, t, iVal, iv); IniWriteInt(f, "HeadDecayStart", iVal);
    pb->GetValue(PB_HEAD_DECAY_END,   t, iVal, iv); IniWriteInt(f, "HeadDecayEnd", iVal);
    pb->GetValue(PB_HEAD_DECAY_REPEAT,t, iVal, iv); IniWriteInt(f, "HeadDecayRepeat", iVal);
    pb->GetValue(PB_TAIL_LIFE_START,  t, iVal, iv); IniWriteInt(f, "TailLifeStart", iVal);
    pb->GetValue(PB_TAIL_LIFE_END,    t, iVal, iv); IniWriteInt(f, "TailLifeEnd", iVal);
    pb->GetValue(PB_TAIL_LIFE_REPEAT, t, iVal, iv); IniWriteInt(f, "TailLifeRepeat", iVal);
    pb->GetValue(PB_TAIL_DECAY_START, t, iVal, iv); IniWriteInt(f, "TailDecayStart", iVal);
    pb->GetValue(PB_TAIL_DECAY_END,   t, iVal, iv); IniWriteInt(f, "TailDecayEnd", iVal);
    pb->GetValue(PB_TAIL_DECAY_REPEAT,t, iVal, iv); IniWriteInt(f, "TailDecayRepeat", iVal);

    // Other
    pb->GetValue(PB_PRIORITY,    t, iVal, iv); IniWriteInt(f, "Priority", iVal);
    pb->GetValue(PB_MODELSPACE,  t, iVal, iv); IniWriteInt(f, "ModelSpace", iVal);
    pb->GetValue(PB_XYQUAD,      t, iVal, iv); IniWriteInt(f, "XYQuad", iVal);

    fclose(f);
    MessageBox(hWnd, _M("Configuration exported successfully."), _M("Wc3Particles2"), MB_OK | MB_ICONINFORMATION);
}

void ConfigDlgProc::ImportConfig(HWND hWnd, IParamBlock2* pb, TimeValue t)
{
    OPENFILENAME ofn = {};
    MCHAR szFile[MAX_PATH] = _M("");
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hWnd;
    ofn.lpstrFilter  = _M("Config File (*.ini)\0*.ini\0All Files\0*.*\0");
    ofn.lpstrFile    = szFile;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle   = _M("Import Particle Configuration");

    if (!GetOpenFileName(&ofn)) return;

    std::string path = NarrowPath(szFile);
    auto ini = IniReadSection(path.c_str());

    if (ini.empty()) {
        MessageBox(hWnd, _M("No [Wc3Particles2] section found in this file."),
                   _M("Import Error"), MB_OK | MB_ICONWARNING);
        return;
    }

    bool dynLoad = (loadDynamic != FALSE);
    theHold.Begin();

    // Emitter
    pb->SetValue(PB_COUNT, t, IniGetInt(ini, "Count", 500));
    ImportAnimFloat(ini, "Speed",      "SpeedUseAnim",      pb, PB_SPEED,      10.0f, dynLoad);
    ImportAnimFloat(ini, "Variation",   "VariationUseAnim",  pb, PB_VARIATION,   0.0f, dynLoad);
    ImportAnimFloat(ini, "ConeAngle",   "ConeAngleUseAnim", pb, PB_ANGLE_Y,     0.0f, dynLoad);
    ImportAnimFloat(ini, "Gravity",     "GravityUseAnim",   pb, PB_GRAVITY,     0.0f, dynLoad);

    // Timing
    pb->SetValue(PB_LIFE, t, IniGetFloat(ini, "Life", 1.0f));
    ImportAnimFloat(ini, "EmissionRate", "EmissionRateUseAnim", pb, PB_INITVEL, 50.0f, dynLoad);
    pb->SetValue(PB_SQUIRT, t, IniGetInt(ini, "Squirt", 0));

    // Size
    ImportAnimFloat(ini, "Width",  "WidthUseAnim",  pb, PB_WIDTH,  0.0f, dynLoad);
    ImportAnimFloat(ini, "Length", "LengthUseAnim", pb, PB_HEIGHT, 0.0f, dynLoad);
    pb->SetValue(PB_LINE_EMIT, t, IniGetInt(ini, "LineEmitter", 0));

    // Texture
    pb->SetValue(PB_BLEND,     t, IniGetInt(ini, "BlendMode", 1));
    pb->SetValue(PB_ROWS,      t, IniGetInt(ini, "Rows", 1));
    pb->SetValue(PB_COLS,      t, IniGetInt(ini, "Cols", 1));
    pb->SetValue(PB_SORT,      t, IniGetInt(ini, "SortZ", 0));
    pb->SetValue(PB_UNSHADED,  t, IniGetInt(ini, "Unshaded", 0));
    pb->SetValue(PB_UNFOGGED,  t, IniGetInt(ini, "Unfogged", 0));
    pb->SetValue(PB_REPLACEABLE_ID, t, IniGetInt(ini, "ReplaceableId", 0));
    {
        std::string pfx = IniGetStr(ini, "PreFix");
        if (!pfx.empty())
            po->m_texturePrefix = WidenPath(pfx);
        std::string tex = IniGetStr(ini, "File");
        if (!tex.empty())
            po->m_particlePath = WidenPath(tex);
    }

    // Particle
    pb->SetValue(PB_TYPE,      t, IniGetInt(ini, "ParticleType", 0));
    pb->SetValue(PB_TAIL_LEN,  t, IniGetFloat(ini, "TailLength", 1.0f));
    pb->SetValue(PB_MIDTIME,   t, IniGetFloat(ini, "MidTime", 0.5f));

    // Colors
    Point3 col;
    col.x = IniGetFloat(ini, "ColorStart_R", 1.0f);
    col.y = IniGetFloat(ini, "ColorStart_G", 1.0f);
    col.z = IniGetFloat(ini, "ColorStart_B", 1.0f);
    pb->SetValue(PB_COLOR_START, t, col);
    col.x = IniGetFloat(ini, "ColorMid_R", 1.0f);
    col.y = IniGetFloat(ini, "ColorMid_G", 1.0f);
    col.z = IniGetFloat(ini, "ColorMid_B", 1.0f);
    pb->SetValue(PB_COLOR_MID, t, col);
    col.x = IniGetFloat(ini, "ColorEnd_R", 1.0f);
    col.y = IniGetFloat(ini, "ColorEnd_G", 1.0f);
    col.z = IniGetFloat(ini, "ColorEnd_B", 1.0f);
    pb->SetValue(PB_COLOR_END, t, col);

    // Alpha, Scale
    pb->SetValue(PB_ALPHA_START, t, IniGetInt(ini, "AlphaStart", 255));
    pb->SetValue(PB_ALPHA_MID,   t, IniGetInt(ini, "AlphaMid", 255));
    pb->SetValue(PB_ALPHA_END,   t, IniGetInt(ini, "AlphaEnd", 0));
    pb->SetValue(PB_SCALE_START, t, IniGetFloat(ini, "ScaleStart", 10.0f));
    pb->SetValue(PB_SCALE_MID,   t, IniGetFloat(ini, "ScaleMid", 10.0f));
    pb->SetValue(PB_SCALE_END,   t, IniGetFloat(ini, "ScaleEnd", 10.0f));

    // Head/Tail UV Anim
    pb->SetValue(PB_HEAD_LIFE_START,  t, IniGetInt(ini, "HeadLifeStart", 0));
    pb->SetValue(PB_HEAD_LIFE_END,    t, IniGetInt(ini, "HeadLifeEnd", 0));
    pb->SetValue(PB_HEAD_LIFE_REPEAT, t, IniGetInt(ini, "HeadLifeRepeat", 1));
    pb->SetValue(PB_HEAD_DECAY_START, t, IniGetInt(ini, "HeadDecayStart", 0));
    pb->SetValue(PB_HEAD_DECAY_END,   t, IniGetInt(ini, "HeadDecayEnd", 0));
    pb->SetValue(PB_HEAD_DECAY_REPEAT,t, IniGetInt(ini, "HeadDecayRepeat", 1));
    pb->SetValue(PB_TAIL_LIFE_START,  t, IniGetInt(ini, "TailLifeStart", 0));
    pb->SetValue(PB_TAIL_LIFE_END,    t, IniGetInt(ini, "TailLifeEnd", 0));
    pb->SetValue(PB_TAIL_LIFE_REPEAT, t, IniGetInt(ini, "TailLifeRepeat", 1));
    pb->SetValue(PB_TAIL_DECAY_START, t, IniGetInt(ini, "TailDecayStart", 0));
    pb->SetValue(PB_TAIL_DECAY_END,   t, IniGetInt(ini, "TailDecayEnd", 0));
    pb->SetValue(PB_TAIL_DECAY_REPEAT,t, IniGetInt(ini, "TailDecayRepeat", 1));

    // Other
    pb->SetValue(PB_PRIORITY,    t, IniGetInt(ini, "Priority", 0));
    pb->SetValue(PB_MODELSPACE,  t, IniGetInt(ini, "ModelSpace", 0));
    pb->SetValue(PB_XYQUAD,      t, IniGetInt(ini, "XYQuad", 0));

    theHold.Accept(_M("Import Particle Config"));
    pb->GetDesc()->InvalidateUI();

    MessageBox(hWnd, _M("Configuration imported successfully."), _M("Wc3Particles2"), MB_OK | MB_ICONINFORMATION);
}

INT_PTR ConfigDlgProc::DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                                UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        CheckDlgButton(hWnd, IDC_CHECK_LOAD_DYNAMIC, loadDynamic ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_CHECK_LOAD_DYNAMIC) {
            loadDynamic = IsDlgButtonChecked(hWnd, IDC_CHECK_LOAD_DYNAMIC);
            return TRUE;
        }
        if (HIWORD(wParam) == BN_CLICKED) {
            IParamBlock2* pb = map->GetParamBlock();
            if (LOWORD(wParam) == IDC_BUTTON_EXPORT)
                ExportConfig(hWnd, pb, t);
            else if (LOWORD(wParam) == IDC_BUTTON_IMPORT)
                ImportConfig(hWnd, pb, t);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/// @}

/// @name Global helper functions
/// @{

// Interpolate a float value over particle lifetime using start/mid/end with midtime
static float InterpOverLife(float u, float midtime, float valStart, float valMid, float valEnd)
{
    u = std::clamp(u, 0.0f, 1.0f);
    if (midtime <= 0.0f) midtime = 0.001f;
    if (midtime >= 1.0f) midtime = 0.999f;
    if (u <= midtime)
        return valStart + (valMid - valStart) * (u / midtime);
    else
        return valMid + (valEnd - valMid) * ((u - midtime) / (1.0f - midtime));
}

// Interpolate a Point3 (color) over particle lifetime using start/mid/end with midtime
static Point3 InterpColorOverLife(float u, float midtime, const Point3& colStart, const Point3& colMid, const Point3& colEnd)
{
    return Point3(InterpOverLife(u, midtime, colStart.x, colMid.x, colEnd.x),
                  InterpOverLife(u, midtime, colStart.y, colMid.y, colEnd.y),
                  InterpOverLife(u, midtime, colStart.z, colMid.z, colEnd.z));
}

/// @}

/// @name Wc3Particles2ParticleDraw implementation
/// @{

BOOL Wc3Particles2ParticleDraw::DrawParticle(GraphicsWindow* gw, ParticleSys& parts, int i)
{
    float u = (life > 0) ? static_cast<float>(parts.ages[i]) / static_cast<float>(life) : 0.0f;
    float sz = InterpOverLife(u, midtime, scaleStart, scaleMid, scaleEnd);
    if (sz < 0.001f)
        return (GetAsyncKeyState(VK_ESCAPE) != 0);

    Point3 col = InterpColorOverLife(u, midtime, colorStart, colorMid, colorEnd);
    gw->setColor(LINE_COLOR, col);

    float mat[4][4];
    Matrix3 invTM;
    int persp = 0;
    float hither = 0, yon = 0;
    gw->getCameraMatrix(mat, &invTM, &persp, &hither, &yon);
    Point3 camPos = invTM.GetRow(3);

    /// In model space, particles are in emitter-local coordinates.
    if (modelSpace)
        camPos = camPos * invEmitterTM;

    bool wantHead = (partType == 0 || partType == 2);
    bool wantTail = (partType == 1 || partType == 2);

    /// HEAD: Billboard quad.
    /// Engine: halfSize = scale * 0.5 (decompiled from CParticleEmitter2::RenderParticle)
    float halfSz = sz * 0.5f;

    if (wantHead) {
        Point3 v0, v1;
        if (xyQuad) {
            // XY-aligned quads: axis-aligned in local/model space (matches engine bit 10)
            v0 = Point3(halfSz, 0.0f, 0.0f);
            v1 = Point3(0.0f, halfSz, 0.0f);
        } else {
            // Camera-facing billboard
            Point3 v = Normalize(camPos - parts[i]);
            Point3 up(0, 0, 1);
            if (fabsf(DotProd(v, up)) > 0.999f)
                up = Point3(0, 1, 0);
            v0 = Normalize(up ^ v) * halfSz;
            v1 = Normalize(v0 ^ v) * halfSz;
        }

        Point3 quad[4];
        quad[0] = parts[i] + v0 + v1;
        quad[1] = parts[i] - v0 + v1;
        quad[2] = parts[i] - v0 - v1;
        quad[3] = parts[i] + v0 - v1;
        gw->polyline(4, quad, NULL, NULL, TRUE, NULL);
    }

    /// TAIL: Normalized direction, fixed length (matches engine).
    /// Engine: tailDir = Normalize(velocity), tailEnd = pos - tailDir * tailLength
    if (wantTail) {
        Point3 head = parts[i];
        float velLen = Length(parts.vels[i]);
        if (velLen > 0.001f) {
            // Tail displacement: velocity-proportional (faster = longer tail).
            // Velocity is in units/tick; multiply by TICKS_PER_SEC to get units/sec,
            // then scale by tailLen.
            Point3 tailDisp = parts.vels[i] * (tailLen * static_cast<float>(TICKS_PER_SEC));
            Point3 tail = head - tailDisp;
            Point3 dir = parts.vels[i] / velLen;

            // Width direction: camera-facing perpendicular to velocity.
            Point3 toCam = Normalize(camPos - (head + tail) * 0.5f);
            Point3 cross = dir ^ toCam;
            float crossLen = Length(cross);
            if (crossLen < 0.001f) {
                Point3 altUp(0, 0, 1);
                if (fabsf(DotProd(dir, altUp)) > 0.999f)
                    altUp = Point3(0, 1, 0);
                cross = dir ^ altUp;
                crossLen = Length(cross);
            }
            Point3 right = (cross / crossLen) * halfSz;

            Point3 quad[4];
            quad[0] = tail - right;
            quad[1] = tail + right;
            quad[2] = head + right;
            quad[3] = head - right;
            gw->polyline(4, quad, NULL, NULL, TRUE, NULL);
        }
    }

    return (GetAsyncKeyState(VK_ESCAPE) != 0);
}

void ParticleCacheData(ParticleSys* dst, ParticleSys* src)
{
    int pc = src->points.Count();
    dst->points.SetCount(pc);

    if (pc > 0) {
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

/// @}

/// @name GenParticle implementation
/// @{

GenParticle::GenParticle()
    : SimpleParticle()
{
    /// PB2 is created by MakeAutoParamBlocks() in the derived class constructor.
    stepSize = 0;
    m_texturePrefix = _M("Textures\\");
}

GenParticle::~GenParticle()
{
    /// PB2 ref cleanup handled by the ReferenceTarget base destructor.
}

/// @name GenParticle — PB2 reference management
/// @{

int GenParticle::NumRefs()
{
    return 1;
}

RefTargetHandle GenParticle::GetReference(int i)
{
    return (i == 0) ? pblock2 : nullptr;
}

void GenParticle::SetReference(int i, RefTargetHandle rtarg)
{
    if (i == 0) {
        pblock2 = static_cast<IParamBlock2*>(rtarg);
        /// Also mirror into SimpleParticle::pblock so the base class null-checks pass.
        /// All parameter reads go through our PB2 via virtual method overrides.
        pblock = reinterpret_cast<IParamBlock*>(rtarg);
    }
}

int GenParticle::NumSubs()
{
    return 1;
}

Animatable* GenParticle::SubAnim(int i)
{
    return (i == 0) ? pblock2 : nullptr;
}

#if MAX_PRODUCT_YEAR_NUMBER >= 2022
MSTR GenParticle::SubAnimName(int i, bool localized)
#else
MSTR GenParticle::SubAnimName(int i)
#endif
{
    return (i == 0) ? MSTR(_M("Parameters")) : MSTR();
}

int GenParticle::NumParamBlocks()
{
    return 1;
}

IParamBlock2* GenParticle::GetParamBlock(int i)
{
    return (i == 0) ? pblock2 : nullptr;
}

IParamBlock2* GenParticle::GetParamBlockByID(BlockID id)
{
    return (id == wc3particles2_params) ? pblock2 : nullptr;
}

RefResult GenParticle::NotifyRefChanged(const Interval& /*changeInt*/,
                                        RefTargetHandle /*hTarget*/,
                                        PartID& /*partID*/,
                                        RefMessage message,
                                        BOOL /*propagate*/)
{
    if (message == REFMSG_CHANGE)
        NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
    return REF_SUCCEED;
}

/// @name GenParticle — particle system core
/// @{

int GenParticle::GetParticleCount()
{
    int count = 0;
    Interval forever = FOREVER;
    pblock2->GetValue(PB_COUNT, 0, count, forever);
    return std::min(count, PARAM_COUNT_MAX);
}

int GenParticle::CountLive()
{
    int c = 0;
    for (int i = 0; i < parts.Count(); i++) {
        if (parts.Alive(i))
            c++;
    }
    return c;
}

void GenParticle::ComputeParticleStart(TimeValue t0, INode* /*node*/)
{
    int count = GetParticleCount();
    parts.SetCount(count, PARTICLE_VELS | PARTICLE_AGES);
    birthPos.resize(count);
    for (int i = 0; i < count; i++) {
        parts.ages[i] = -1;
        birthPos[i] = Point3(0, 0, 0);
    }
    tvalid = t0;
    valid  = TRUE;
}

void GenParticle::BirthParticle(INode* node, TimeValue bt, int index, TimeValue dt)
{
    float width = 0.0f, height = 0.0f, initVel = 0.0f, var = 0.0f;
    float latitude = 0.0f;
    int line_emitter = 0;
    constexpr float kDegToRad = 0.017453292f;
    constexpr float kPi = 3.14159265f;

    Interval forever = FOREVER;

    pblock2->GetValue(PB_WIDTH, bt, width, forever);
    pblock2->GetValue(PB_HEIGHT, bt, height, forever);
    pblock2->GetValue(PB_SPEED, bt, initVel, forever);
    pblock2->GetValue(PB_VARIATION, bt, var, forever);
    pblock2->GetValue(PB_ANGLE_Y, bt, latitude, forever);
    pblock2->GetValue(PB_LINE_EMIT, bt, line_emitter, forever);

    BOOL modelSpace = FALSE;
    pblock2->GetValue(PB_MODELSPACE, bt, modelSpace, forever);
    Matrix3 tm = modelSpace ? Matrix3(1) : node->GetObjTMBeforeWSM(bt);

    // Use member RNG (std::mt19937) instead of global srand/rand
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    auto randFloat = [&]() { return dist01(m_rng); };
    auto randSigned = [&]() { return randFloat() * 2.0f - 1.0f; };

    // Speed with variation: speed * (1 + random[-1,1] * variation)
    float speed = initVel * (1.0f + randSigned() * var);
    speed /= static_cast<float>(TICKS_PER_SEC);  // unit conversion: PB units to world-units-per-tick

    // Rotation angles (symmetric random, degrees to radians)
    float rotLat = latitude * randSigned() * kDegToRad;

    // Initial velocity along +Z, rotated by latitude/longitude
    // PE2 has no longitude in model data — derived from LineEmitter flag:
    //   lineEmitter → longitude = 0 (XZ plane only)
    //   otherwise   → longitude = π (full circle)
    float velX, velY, velZ;
    if (line_emitter) {
        velX = speed * sinf(rotLat);
        velY = 0.0f;
        velZ = speed * cosf(rotLat);
    } else {
        float rotLon = kPi * randSigned();
        velX = speed * sinf(rotLat) * cosf(rotLon);
        velY = speed * sinf(rotLat) * sinf(rotLon);
        velZ = speed * cosf(rotLat);
    }

    Point3 vel(velX, velY, velZ);

    // Sub-frame age randomization: distribute births across the time step
    int subFrameAge = static_cast<int>(randFloat() * static_cast<float>(dt));
    parts.ages[index] = subFrameAge;

    vel = VectorTransform(tm, vel);
    parts.vels[index] = vel;

    Point3 pos;
    pos.x = -width / 2.0f + randFloat() * width;
    pos.y = -height / 2.0f + randFloat() * height;
    pos.z = 0.0f;

    pos = pos * tm;
    parts[index] = pos;
    if (index < static_cast<int>(birthPos.size()))
        birthPos[index] = pos;
}

void GenParticle::UpdateParticles(TimeValue t, INode* node)
{
    /// Only emit particles while the emitter node is visible in the scene.
    if (node && (node->IsNodeHidden() || node->GetVisibility(t) <= 0.0f)) {
        parts.FreeAll();
        tvalid = t;
        valid  = FALSE;
        return;
    }

    constexpr int PARTICLE_SEED = static_cast<int>(0x8d6a65bc);

    TimeValue t0 = 0;
    float life_sec = 0.0f;
    int total = 0;
    int birth = 0;
    float brate = 1.0f;
    float brateFactor = 1.0f;
    int squirt = 0;
    Point3 force;

    TimeValue oneframe = GetTicksPerFrame();
    if (stepSize != oneframe) {
        stepSize = oneframe;
        valid = FALSE;
    }

    Interval forever = FOREVER;
    pblock2->GetValue(PB_LIFE, t, life_sec, forever);
    pblock2->GetValue(PB_SQUIRT, t, squirt, forever);
    TimeValue life = static_cast<TimeValue>(life_sec * static_cast<float>(TICKS_PER_SEC));
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
            pblock2->GetValue(PB_INITVEL, tvalid, brate, forever);

            if (squirt) {
        /// Squirt mode: scan emission-rate keys to detect rising edges and
        /// burst the full particle count at each transition from zero.
                brate  = 0.0f;
                birth  = 0;
                Control* emitCtrl = pblock2->GetControllerByID(PB_INITVEL);
                if (emitCtrl) {
                    int numKeys = emitCtrl->NumKeys();
                    for (int k = numKeys - 1; k >= 0; k--) {
                        TimeValue keyTime = emitCtrl->GetKeyTime(k);
                        if (keyTime > tvalid) continue;

                        float valAtKey = 0.0f;
                        pblock2->GetValue(PB_INITVEL, keyTime, valAtKey, forever);

                        if (valAtKey <= 0.0f) continue;

                        bool isTransition = (k == 0);
                        if (!isTransition) {
                            float prevVal = 0.0f;
                            pblock2->GetValue(PB_INITVEL, emitCtrl->GetKeyTime(k - 1), prevVal, forever);
                            isTransition = (prevVal <= 0.0f);
                        }

                        if (isTransition) {
                            float lifeAtKey = 0.0f;
                            pblock2->GetValue(PB_LIFE, keyTime, lifeAtKey, forever);
                            TimeValue lifeTicks = static_cast<TimeValue>(
                                lifeAtKey * static_cast<float>(TICKS_PER_SEC));
                            if (keyTime + lifeTicks >= tvalid) {
                                brate = valAtKey;
                                if (keyTime > tvalid - dt && keyTime <= tvalid) {
                                    birth = total;
                                }
                            }
                            break;
                        }
                    }
                }
            } else {
                brate /= static_cast<float>(TICKS_PER_SEC);
                birth = static_cast<int>(static_cast<float>(tvalid - t0) * brate * brateFactor)
                      - static_cast<int>(static_cast<float>(tvalid - t0 - dt) * brate * brateFactor);
            }
        }

        /// Age existing particles forward by dt ticks; kill any that exceed their lifetime.
        for (int j = 0; j < parts.Count(); j++) {
            if (!parts.Alive(j)) continue;
            parts.ages[j] += dt;
            if (parts.ages[j] >= life) {
                parts.ages[j] = -1;
            }
        }

        /// Deterministic RNG seed derived from current time, matching the
        /// permutation-table approach used by the original emitter.
        int seed1 = 1200 * tvalid / TICKS_PER_SEC;
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

        /// Birth new particles into the first available dead slots.
        for (int j = 0; j < parts.Count(); j++) {
            if (born >= birth) break;
            if (!parts.Alive(j)) {
                BirthParticle(node, tvalid, j, dt);
                born++;
            }
        }

        /// Apply registered force fields on full frames.
        if (fullframe) {
            for (int i = 0; i < fields.Count(); i++) {
                for (int m = 0; m < parts.Count(); m++) {
                    if (!parts.Alive(m)) continue;
                    force = fields[i]->Force(tvalid, parts[m], parts.vels[m], m);
                    parts.vels[m] += force * static_cast<float>(dt);
                }
            }
        }

        /// Apply gravity and integrate particle positions (kinematic: pos += vel*dt + 0.5*a*dt²).
        {
            float gravity = 0.0f;
            pblock2->GetValue(PB_GRAVITY, tvalid, gravity, forever);
            float gAccel = -gravity / (static_cast<float>(TICKS_PER_SEC) * static_cast<float>(TICKS_PER_SEC));
            float fdt = static_cast<float>(dt);
            for (int n = 0; n < parts.Count(); n++) {
                if (!parts.Alive(n)) continue;
                parts[n] += parts.vels[n] * fdt + Point3(0.0f, 0.0f, gAccel * 0.5f * fdt * fdt);
                parts.vels[n].z += gAccel * fdt;
            }
        }
    }

    assert(tvalid == t);
}

void GenParticle::BuildEmitter(TimeValue t, Mesh& mesh)
{
    float width = 0.0f, height = 0.0f;

    mvalid = Interval(TIME_NegInfinity, TIME_PosInfinity);
    pblock2->GetValue(PB_WIDTH, t, width, mvalid);
    pblock2->GetValue(PB_HEIGHT, t, height, mvalid);

    float hw = width * 0.5f;
    float hh = height * 0.5f;

    mesh.setNumVerts(6);
    mesh.setNumFaces(6);

    mesh.setVert(0, Point3(-hw, -hh, 0.0f));
    mesh.setVert(1, Point3( hw, -hh, 0.0f));
    mesh.setVert(2, Point3( hw,  hh, 0.0f));
    mesh.setVert(3, Point3(-hw,  hh, 0.0f));
    mesh.setVert(4, Point3(0.0f, 0.0f, 0.0f));
    mesh.setVert(5, Point3(0.0f, 0.0f, (hw + hh) / 2.0f));

    mesh.faces[0].setEdgeVisFlags(1, 0, 1);
    mesh.faces[0].setSmGroup(1);
    mesh.faces[0].setVerts(0, 1, 3);

    mesh.faces[1].setEdgeVisFlags(1, 1, 0);
    mesh.faces[1].setSmGroup(1);
    mesh.faces[1].setVerts(1, 2, 3);

    mesh.faces[2].setEdgeVisFlags(1, 1, 0);
    mesh.faces[2].setSmGroup(1);
    mesh.faces[2].setVerts(4, 5, 4);

    mesh.faces[3].setEdgeVisFlags(1, 0, 1);
    mesh.faces[3].setSmGroup(1);
    mesh.faces[3].setVerts(0, 3, 1);

    mesh.faces[4].setEdgeVisFlags(0, 1, 1);
    mesh.faces[4].setSmGroup(1);
    mesh.faces[4].setVerts(1, 3, 2);

    mesh.faces[5].setEdgeVisFlags(1, 0, 0);
    mesh.faces[5].setSmGroup(1);
    mesh.faces[5].setVerts(5, 4, 4);

    mesh.InvalidateGeomCache();
}

/**
 * @brief Draws the emitter rectangle and direction arrow as wireframe polylines.
 */
static void DrawEmitterWireframe(GraphicsWindow* gw, float width, float height)
{
    float hw = width * 0.5f;
    float hh = height * 0.5f;
    float az = (hw + hh) * 0.5f;

    Point3 rect[5];
    rect[0] = Point3(-hw, -hh, 0.0f);
    rect[1] = Point3( hw, -hh, 0.0f);
    rect[2] = Point3( hw,  hh, 0.0f);
    rect[3] = Point3(-hw,  hh, 0.0f);
    rect[4] = rect[0];
    gw->polyline(5, rect, nullptr, nullptr, FALSE, nullptr);

    Point3 arrow[2];
    arrow[0] = Point3(0.0f, 0.0f, 0.0f);
    arrow[1] = Point3(0.0f, 0.0f, az);
    gw->polyline(2, arrow, nullptr, nullptr, FALSE, nullptr);
}

/**
 * @brief Draws the emitter wireframe and live particles in the viewport.
 *
 * Overrides SimpleParticle::Display() because the base implementation reads
 * dimensions directly from the PB1 @c pblock pointer, which is not valid when
 * using a PB2. Instead this method reads from @c pblock2 for the emitter
 * rectangle, then delegates particle rendering to the base Update()/Render() path.
 */
int GenParticle::Display(TimeValue t, INode* inode, ViewExp* vpt, int flags)
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

    float width = 0.0f, height = 0.0f;
    Interval ivalid = FOREVER;
    pblock2->GetValue(PB_WIDTH, t, width, ivalid);
    pblock2->GetValue(PB_HEIGHT, t, height, ivalid);

    DrawEmitterWireframe(gw, width, height);

    gw->setRndLimits(rlim0);

    // --- Particles ---
    Update(t, inode);
    MarkerType mt = GetMarkerType();

    BOOL modelSpace = FALSE;
    Interval msValid = FOREVER;
    pblock2->GetValue(PB_MODELSPACE, t, modelSpace, msValid);
    if (modelSpace) {
        gw->setTransform(tm);       // particles in local space — apply emitter TM
        theWc3Particles2Draw.emitterTM = tm;
        theWc3Particles2Draw.invEmitterTM = Inverse(tm);
    } else {
        gw->setTransform(Matrix3(1));   // particles in world space
    }
    parts.Render(gw, mt);

    return 0;
}

int GenParticle::HitTest(TimeValue t, INode* inode, int type, int crossing,
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

    /// Hit-test the emitter wireframe by drawing polylines in pick mode.
    float width = 0.0f, height = 0.0f;
    Interval ivalid = FOREVER;
    pblock2->GetValue(PB_WIDTH, t, width, ivalid);
    pblock2->GetValue(PB_HEIGHT, t, height, ivalid);

    DrawEmitterWireframe(gw, width, height);

    if (gw->checkHitCode()) {
        gw->setRndLimits(rlim0);
        return TRUE;
    }

    /// Also hit-test mesh faces (the interior of the emitter rectangle).
    UpdateMesh(t);
    if (mesh.select(gw, nullptr, &hitRegion)) {
        gw->setRndLimits(rlim0);
        return TRUE;
    }

    gw->setRndLimits(rlim0);
    return FALSE;
}

/**
 * @brief Computes the local-space bounding box from PB2 width/height values.
 *
 * Overrides SimpleParticle::GetLocalBoundBox() because the base implementation
 * reads dimensions from the PB1 @c pblock member, which is not a valid
 * IParamBlock pointer when PB2 is in use. Without a correct bounding box,
 * 3ds Max viewport culling would suppress Display() entirely.
 */
void GenParticle::GetLocalBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box)
{
    if (!pblock2) {
        box = Box3(Point3(-10, -10, 0), Point3(10, 10, 10));
        return;
    }

    float width = 0.0f, height = 0.0f;
    Interval ivalid = FOREVER;
    pblock2->GetValue(PB_WIDTH, t, width, ivalid);
    pblock2->GetValue(PB_HEIGHT, t, height, ivalid);

    float hw = width * 0.5f;
    float hh = height * 0.5f;
    float az = (hw + hh) * 0.5f;   // arrow tip Z

    box = Box3(Point3(-hw, -hh, 0.0f), Point3(hw, hh, az));
    box.EnlargeBy(10.0f);
}

void GenParticle::GetWorldBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box)
{
    GetLocalBoundBox(t, inode, vpt, box);
    Matrix3 tm = inode->GetObjectTM(t);
    box = box * tm;
}

Interval GenParticle::GetValidity(TimeValue t)
{
    return Interval(t, t);
}

void GenParticle::InvalidateUI()
{
    wc3particles2_param_blk.InvalidateUI();
}

ParamDimension* GenParticle::GetParameterDim(int pbIndex)
{
    switch (pbIndex) {
    case PB_WIDTH:
    case PB_HEIGHT:
        return stdWorldDim;
    case PB_MIDTIME:
        return stdNormalizedDim;
    case PB_COLOR_START:
    case PB_COLOR_MID:
    case PB_COLOR_END:
        return stdColorDim;
    case PB_ALPHA_START:
    case PB_ALPHA_MID:
    case PB_ALPHA_END:
        return stdColor255Dim;
    case PB_TAIL_LEN:
        return stdTimeDim;
    default:
        return defaultDim;
    }
}

#if MAX_PRODUCT_YEAR_NUMBER >= 2022
MSTR GenParticle::GetParameterName(int pbIndex, bool localized)
#else
MSTR GenParticle::GetParameterName(int pbIndex)
#endif
{
    const MCHAR* s = nullptr;
    switch (pbIndex) {
    case PB_COUNT:      s = GetString(IDS_PARAM_COUNT); break;
    case PB_SPEED:      s = GetString(IDS_PARAM_SPEED); break;
    case PB_VARIATION:  s = GetString(IDS_PARAM_VARIATION); break;
    case PB_LIFE:       s = GetString(IDS_PARAM_LIFE); break;
    case PB_WIDTH:      s = GetString(IDS_WIDTH); break;
    case PB_HEIGHT:     s = GetString(IDS_HEIGHT); break;
    case PB_INITVEL:    s = GetString(IDS_PARAM_INITVEL); break;
    case PB_ANGLE_Y:    s = GetString(IDS_PARAM_ANGLE_Y); break;
    case PB_MIDTIME:    s = GetString(IDS_PARAM_MIDTIME); break;
    case PB_COLOR_START:s = GetString(IDS_PARAM_COLOR_START); break;
    case PB_COLOR_MID:  s = GetString(IDS_PARAM_COLOR_MID); break;
    case PB_COLOR_END:  s = GetString(IDS_PARAM_COLOR_END); break;
    case PB_ALPHA_START:    s = GetString(IDS_PARAM_ALPHA_START); break;
    case PB_ALPHA_MID:    s = GetString(IDS_PARAM_ALPHA_MID); break;
    case PB_ALPHA_END:    s = GetString(IDS_PARAM_ALPHA_END); break;
    case PB_SCALE_START: s = GetString(IDS_PARAM_SCALE_START); break;
    case PB_SCALE_MID:   s = GetString(IDS_PARAM_SCALE_MID); break;
    case PB_SCALE_END:   s = GetString(IDS_PARAM_SCALE_END); break;
    case PB_HEAD_LIFE_START:s = GetString(IDS_PARAM_HEAD_LIFE_START); break;
    case PB_HEAD_LIFE_REPEAT:  s = GetString(IDS_PARAM_HEAD_LIFE_REPEAT); break;
    case PB_HEAD_LIFE_END:  s = GetString(IDS_PARAM_HEAD_LIFE_END); break;
    case PB_HEAD_DECAY_START:   s = GetString(IDS_PARAM_HEAD_DECAY_START); break;
    case PB_HEAD_DECAY_REPEAT:     s = GetString(IDS_PARAM_HEAD_DECAY_REPEAT); break;
    case PB_HEAD_DECAY_END:   s = GetString(IDS_PARAM_HEAD_DECAY_END); break;
    case PB_TAIL_LEN:   s = GetString(IDS_PARAM_TAIL_LEN); break;
    case PB_TYPE:       s = GetString(IDS_PARAM_TYPE); break;
    case PB_ROWS:       s = _T("Texture Rows"); break;
    case PB_COLS:       s = _T("Texture Columns"); break;
    case PB_TAIL_LIFE_START:      s = GetString(IDS_PARAM_TAIL_LIFE_START); break;
    case PB_TAIL_LIFE_REPEAT:      s = GetString(IDS_PARAM_TAIL_LIFE_REPEAT); break;
    case PB_TAIL_LIFE_END:      s = GetString(IDS_PARAM_TAIL_LIFE_END); break;
    case PB_TAIL_DECAY_START:      s = GetString(IDS_PARAM_TAIL_DECAY_START); break;
    case PB_TAIL_DECAY_REPEAT:      s = GetString(IDS_PARAM_TAIL_DECAY_REPEAT); break;
    case PB_TAIL_DECAY_END:      s = GetString(IDS_PARAM_TAIL_DECAY_END); break;
    case PB_SQUIRT:     s = GetString(IDS_PARAM_SQUIRT); break;
    case PB_BLEND:      s = _T("Blend Mode"); break;
    case PB_GRAVITY:    s = GetString(IDS_PARAM_GRAVITY); break;
    case PB_SORT:       s = _T("Sort Primitives"); break;
    case PB_LINE_EMIT:  s = _T("Line Emitter"); break;
    case PB_UNSHADED:   s = _T("Unshaded"); break;
    case PB_LATITUDE:   s = _T("Latitude (deprecated)"); break;
    case PB_PRIORITY:   s = _T("Priority Plane"); break;
    case PB_UNFOGGED:   s = _T("Unfogged"); break;
    case PB_MODELSPACE: s = _T("Model Space"); break;
    case PB_XYQUAD:     s = _T("XY Quad"); break;
    case PB_REPLACEABLE_ID: s = _T("Replaceable Texture ID"); break;
    default:            break;
    }
    return MSTR(s ? s : _T(""));
}

Point3 GenParticle::ParticlePosition(TimeValue /*t*/, int i)
{
    return parts.points[i];
}

Point3 GenParticle::ParticleVelocity(TimeValue /*t*/, int i)
{
    return parts.vels[i];
}

int GenParticle::ParticleLife(TimeValue t, int /*i*/)
{
    float life_sec = 0.0f;
    Interval forever = FOREVER;
    pblock2->GetValue(PB_LIFE, t, life_sec, forever);
    return static_cast<int>(life_sec * TICKS_PER_SEC);
}

int GenParticle::RenderEnd(TimeValue /*t*/)
{
    ClearAFlag(A_PLUGIN1);
    ParticleInvalid();
    NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
    return 0;
}

int GenParticle::RenderBegin(TimeValue /*t*/, ULONG /*flags*/)
{
    SetAFlag(A_PLUGIN1);
    ParticleInvalid();
    NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
    return 0;
}

void GenParticle::RescaleWorldUnits(float f)
{
    if (TestAFlag(A_WORK1))
        return;

    SimpleParticle::RescaleWorldUnits(f);

    // PB2 doesn't have RescaleParam — manually rescale world-dimension params
    if (pblock2) {
        float val;
        Interval iv = FOREVER;
        pblock2->GetValue(PB_WIDTH, 0, val, iv);
        pblock2->SetValue(PB_WIDTH, 0, val * f);
        iv = FOREVER;
        pblock2->GetValue(PB_HEIGHT, 0, val, iv);
        pblock2->SetValue(PB_HEIGHT, 0, val * f);
    }
}

CreateMouseCallBack* GenParticle::GetCreateMouseCallBack()
{
    emitterCallback.rain = this;
    return &emitterCallback;
}

void GenParticle::MapKeys(TimeMap* map, DWORD flags)
{
    Animatable::MapKeys(map, flags);
}

void GenParticle::DeleteThis()
{
    delete this;
}

/**
 * @brief Saves the particle texture path to a .MAX chunk stream.
 *
 * Writes a single chunk (ID 1000) containing the ANSI-encoded path length
 * followed by the path bytes. ANSI encoding is preserved for compatibility
 * with scene files written by earlier versions of the plugin.
 */

IOResult GenParticle::Save(ISave* isave)
{
    ULONG nb = 0;

    CStr ansiPath(m_particlePath.ToCStr());
    int len = static_cast<int>(strlen(ansiPath.data())) + 1;

    isave->BeginChunk(1000);
    isave->Write(&len, sizeof(int), &nb);
    isave->Write(ansiPath.data(), len, &nb);
    isave->EndChunk();

    CStr ansiPrefix(m_texturePrefix.ToCStr());
    int plen = static_cast<int>(strlen(ansiPrefix.data())) + 1;

    isave->BeginChunk(1001);
    isave->Write(&plen, sizeof(int), &nb);
    isave->Write(ansiPrefix.data(), plen, &nb);
    isave->EndChunk();

    return IO_OK;
}

IOResult GenParticle::Load(ILoad* iload)
{
    IOResult res;
    while ((res = iload->OpenChunk()) == IO_OK) {
        if (iload->CurChunkID() == 1000)
            LoadAnsiChunk(iload, m_particlePath);
        else if (iload->CurChunkID() == 1001)
            LoadAnsiChunk(iload, m_texturePrefix);
        iload->CloseChunk();
    }

    return IO_OK;
}

void* GenParticle::GetInterface(ULONG id)
{
    if (id == WC3P2_TEXTURE_PATH_IID)   return &m_particlePath;
    if (id == WC3P2_TEXTURE_PREFIX_IID) return &m_texturePrefix;
    return SimpleParticle::GetInterface(id);
}

void GenParticle::BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev)
{
    SimpleParticle::BeginEditParams(ip, flags, prev);
    GetWc3Particles2Desc()->BeginEditParams(ip, this, flags, prev);
    /// Install custom DlgProc for rollouts that need special handling.
    wc3particles2_param_blk.SetUserDlgProc(MAP_EMITTER, new Wc3Particles2DlgProc(this));
    wc3particles2_param_blk.SetUserDlgProc(MAP_TIMING,  new Wc3Particles2DlgProc(this));
    wc3particles2_param_blk.SetUserDlgProc(MAP_SIZE,    new Wc3Particles2DlgProc(this));
    wc3particles2_param_blk.SetUserDlgProc(MAP_TEXTURE, new Wc3Particles2DlgProc(this));
    wc3particles2_param_blk.SetUserDlgProc(MAP_CONFIG,  new ConfigDlgProc(this));
}

void GenParticle::EndEditParams(IObjParam* ip, ULONG flags, Animatable* next)
{
    SimpleParticle::EndEditParams(ip, flags, next);
    GetWc3Particles2Desc()->EndEditParams(ip, this, flags, next);
}

/// @name Wc3Particles2Particle implementation
/// @{

Wc3Particles2Particle::Wc3Particles2Particle()
    : GenParticle()
{
    GetWc3Particles2Desc()->MakeAutoParamBlocks(this);
    assert(pblock2);
}

Wc3Particles2Particle::~Wc3Particles2Particle() = default;

Class_ID Wc3Particles2Particle::ClassID()
{
    return WC3PARTICLES2_CLASS_ID;
}

#if MAX_PRODUCT_YEAR_NUMBER >= 2022
const MCHAR* Wc3Particles2Particle::GetObjectName(bool localized) const
#else
const MCHAR* Wc3Particles2Particle::GetObjectName()
#endif
{
    return GetString(IDS_OBJECT_NAME);
}

BOOL Wc3Particles2Particle::IsInstanceDependent()
{
    return TRUE;
}

float Wc3Particles2Particle::ParticleSize(TimeValue t, int i)
{
    float life_sec = 0.0f, midtime = 0.5f;
    float scaleStart = 10.0f, scaleMid = 10.0f, scaleEnd = 10.0f;
    Interval forever = FOREVER;
    pblock2->GetValue(PB_LIFE, t, life_sec, forever);
    pblock2->GetValue(PB_MIDTIME, t, midtime, forever);
    pblock2->GetValue(PB_SCALE_START, t, scaleStart, forever);
    pblock2->GetValue(PB_SCALE_MID, t, scaleMid, forever);
    pblock2->GetValue(PB_SCALE_END, t, scaleEnd, forever);
    int life = static_cast<int>(life_sec * TICKS_PER_SEC);
    if (life <= 0) life = 1;
    float u = static_cast<float>(parts.ages[i]) / static_cast<float>(life);
    return InterpOverLife(u, midtime, scaleStart, scaleMid, scaleEnd);
}

int Wc3Particles2Particle::ParticleCenter(TimeValue /*t*/, int /*i*/)
{
    return 2;   // CENTER
}

MarkerType Wc3Particles2Particle::GetMarkerType()
{
    theWc3Particles2Draw.obj = this;
    float life_sec = 0.0f;
    float tailLen = 0.0f;
    int partType = 0;
    float midtime = 0.5f;
    float scaleStart = 10.0f, scaleMid = 10.0f, scaleEnd = 10.0f;
    Point3 colorStart(1,1,1), colorMid(1,1,1), colorEnd(1,1,1);
    Interval forever = FOREVER;
    pblock2->GetValue(PB_LIFE, 0, life_sec, forever);
    pblock2->GetValue(PB_TYPE, 0, partType, forever);
    pblock2->GetValue(PB_TAIL_LEN, 0, tailLen, forever);
    pblock2->GetValue(PB_MIDTIME, 0, midtime, forever);
    pblock2->GetValue(PB_SCALE_START, 0, scaleStart, forever);
    pblock2->GetValue(PB_SCALE_MID, 0, scaleMid, forever);
    pblock2->GetValue(PB_SCALE_END, 0, scaleEnd, forever);
    pblock2->GetValue(PB_COLOR_START, 0, colorStart, forever);
    pblock2->GetValue(PB_COLOR_MID, 0, colorMid, forever);
    pblock2->GetValue(PB_COLOR_END, 0, colorEnd, forever);
    theWc3Particles2Draw.life = static_cast<int>(life_sec * TICKS_PER_SEC);
    theWc3Particles2Draw.partType = partType;
    theWc3Particles2Draw.tailLen = tailLen;
    theWc3Particles2Draw.midtime = midtime;
    theWc3Particles2Draw.scaleStart = scaleStart;
    theWc3Particles2Draw.scaleMid = scaleMid;
    theWc3Particles2Draw.scaleEnd = scaleEnd;
    theWc3Particles2Draw.colorStart = colorStart;
    theWc3Particles2Draw.colorMid = colorMid;
    theWc3Particles2Draw.colorEnd = colorEnd;
    BOOL xyQuad = FALSE;
    pblock2->GetValue(PB_XYQUAD, 0, xyQuad, forever);
    theWc3Particles2Draw.xyQuad = xyQuad;
    BOOL modelSpace = FALSE;
    pblock2->GetValue(PB_MODELSPACE, 0, modelSpace, forever);
    theWc3Particles2Draw.modelSpace = modelSpace;
    parts.SetCustomDraw(&theWc3Particles2Draw);
    return static_cast<MarkerType>(0);
}

ReferenceTarget* Wc3Particles2Particle::Clone(RemapDir& remap)
{
    auto* newob = new Wc3Particles2Particle();
    newob->ReplaceReference(0, remap.CloneRef(pblock2));
    newob->m_particlePath = m_particlePath;
    newob->m_texturePrefix = m_texturePrefix;
    newob->mvalid.SetEmpty();
    newob->tvalid = 0;
    newob->valid = FALSE;
    BaseClone(this, newob, remap);
    return newob;
}

Mesh* Wc3Particles2Particle::GetRenderMesh(TimeValue t, INode* inode, View& view,
                                      BOOL& needDelete)
{
    float life_sec = 0.0f, midtime = 0.5f;
    float scaleStart = 10.0f, scaleMid = 10.0f, scaleEnd = 10.0f;
    Point3 colorStart(1,1,1), colorMid(1,1,1), colorEnd(1,1,1);
    Interval forever = FOREVER;
    pblock2->GetValue(PB_LIFE, t, life_sec, forever);
    pblock2->GetValue(PB_MIDTIME, t, midtime, forever);
    pblock2->GetValue(PB_SCALE_START, t, scaleStart, forever);
    pblock2->GetValue(PB_SCALE_MID, t, scaleMid, forever);
    pblock2->GetValue(PB_SCALE_END, t, scaleEnd, forever);
    pblock2->GetValue(PB_COLOR_START, t, colorStart, forever);
    pblock2->GetValue(PB_COLOR_MID, t, colorMid, forever);
    pblock2->GetValue(PB_COLOR_END, t, colorEnd, forever);
    int life = static_cast<int>(life_sec * TICKS_PER_SEC);
    if (life <= 0) life = 1;

    BOOL xyQuad = FALSE;
    pblock2->GetValue(PB_XYQUAD, t, xyQuad, forever);

    BOOL modelSpace = FALSE;
    pblock2->GetValue(PB_MODELSPACE, t, modelSpace, forever);

    // In model space, particles are already local — no inverse TM needed
    Matrix3 tm = modelSpace ? Matrix3(1) : Inverse(inode->GetObjTMAfterWSM(t));
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

    // XYQuad mode: 4 verts, 2 faces per particle (proper quad)
    // Normal mode: 6 verts, 2 faces per particle (radial hexagon)
    int numVPerPart, numFPerPart;
    if (xyQuad) {
        numVPerPart = 4;
        numFPerPart = 2;
    } else {
        numVPerPart = 6;
        numFPerPart = 2;
    }

    pm->setNumFaces(numFPerPart * count);
    pm->setNumVerts(numVPerPart * count);
    pm->setNumTVerts(numVPerPart * count);
    pm->setNumTVFaces(numFPerPart * count);
    pm->setNumVertCol(numVPerPart * count);
    pm->setNumVCFaces(numFPerPart * count);

    for (int i = 0; i < parts.Count(); i++) {
        if (!parts.Alive(i))
            continue;

        float u = static_cast<float>(parts.ages[i]) / static_cast<float>(life);
        float sz = InterpOverLife(u, midtime, scaleStart, scaleMid, scaleEnd);
        Point3 col = InterpColorOverLife(u, midtime, colorStart, colorMid, colorEnd);

        if (xyQuad) {
            // XY-aligned quad: flat on XY plane
            Point3 p = parts[i];
            pm->verts[ix + 0] = (p + Point3(-sz, -sz, 0.0f)) * tm;
            pm->verts[ix + 1] = (p + Point3( sz, -sz, 0.0f)) * tm;
            pm->verts[ix + 2] = (p + Point3( sz,  sz, 0.0f)) * tm;
            pm->verts[ix + 3] = (p + Point3(-sz,  sz, 0.0f)) * tm;

            pm->tVerts[ix + 0] = Point3(0.0f, 0.0f, 0.0f);
            pm->tVerts[ix + 1] = Point3(1.0f, 0.0f, 0.0f);
            pm->tVerts[ix + 2] = Point3(1.0f, 1.0f, 0.0f);
            pm->tVerts[ix + 3] = Point3(0.0f, 1.0f, 0.0f);

            pm->faces[nx].setSmGroup(0);
            pm->faces[nx].setVerts(ix, ix + 1, ix + 2);
            pm->faces[nx].setMatID(static_cast<MtlID>(i));
            pm->faces[nx].setEdgeVisFlags(1, 1, 0);
            pm->faces[nx + 1].setSmGroup(0);
            pm->faces[nx + 1].setVerts(ix, ix + 2, ix + 3);
            pm->faces[nx + 1].setMatID(static_cast<MtlID>(i));
            pm->faces[nx + 1].setEdgeVisFlags(0, 1, 1);

            pm->tvFace[nx].setTVerts(ix, ix + 1, ix + 2);
            pm->tvFace[nx + 1].setTVerts(ix, ix + 2, ix + 3);
            for (int q = 0; q < 4; q++)
                pm->vertCol[ix + q] = col;
            pm->vcFace[nx].setTVerts(ix, ix + 1, ix + 2);
            pm->vcFace[nx + 1].setTVerts(ix, ix + 2, ix + 3);
        } else {
            for (int j = 0; j < numVPerPart; j++) {
                float angle = (TWOPI * static_cast<float>(j)) / static_cast<float>(numVPerPart);
                pm->verts[j + ix].x = cosf(angle) * sz;
                pm->verts[j + ix].y = 0.0f;
                pm->verts[j + ix].z = sinf(angle) * sz;

                pm->tVerts[j + ix].x = cosf(angle);
                pm->tVerts[j + ix].y = sinf(angle);
                pm->tVerts[j + ix].z = 0.0f;

                pm->verts[j + ix] += parts[i];
                pm->tVerts[j + ix] *= 0.5f;
                pm->tVerts[j + ix] += Point3(0.5f, 0.5f, 0.0f);
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
            for (int q = 0; q < numVPerPart; q++)
                pm->vertCol[ix + q] = col;
            pm->vcFace[nx].setTVerts(ix, ix + 2, ix + 4);
            pm->vcFace[nx + 1].setTVerts(ix + 1, ix + 3, ix + 5);
        }

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
