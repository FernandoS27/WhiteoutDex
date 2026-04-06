/**
 * @file    Ribbon.cpp
 * @brief   Wc3Ribbon — Ribbon emitter particle system plugin for 3ds Max.
 * @note    Requires 3ds Max 2016+ SDK (x64, Unicode, ParamBlock2).
 */

#include "Ribbon.h"
#include <cassert>

// =========================================================================
// Globals
// =========================================================================

HINSTANCE hInstance;
static MCHAR buf[256];

static RibbonObjClassDesc       ribbonObjDesc;
static RibbonObjCreateCallBack  pointHelpCreateCB;

IObjParam*    RibbonObject::s_ip      = nullptr;
RibbonObject* RibbonObject::s_editOb  = nullptr;

// =========================================================================
// Utility functions
// =========================================================================

const MCHAR* GetString(UINT id)
{
    if (!hInstance)
        return nullptr;
    if (LoadString(hInstance, id, buf, _countof(buf)))
        return buf;
    return nullptr;
}

ClassDesc2* GetRibbonDesc()
{
    return &ribbonObjDesc;
}

// =========================================================================
// DLL entry / exit
// =========================================================================
BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID lpvReserved)
{
    hInstance = hinstDLL;
    return TRUE;
}

// =========================================================================
// 3ds Max plugin exports
// =========================================================================

__declspec(dllexport) const TCHAR* LibDescription()
{
    return GetString(IDS_LIBDESCRIPTION);
}

__declspec(dllexport) int LibNumberClasses()
{
    return 1;
}

__declspec(dllexport) ClassDesc* LibClassDesc(int i)
{
    switch (i) {
        case 0:  return GetRibbonDesc();
        default: return nullptr;
    }
}

__declspec(dllexport) ULONG LibVersion()
{
    return VERSION_3DSMAX;
}

__declspec(dllexport) ULONG CanAutoDefer()
{
    return 1;
}

// =========================================================================
// ParamBlockDesc2 — P_MULTIMAP with two rollouts
// =========================================================================

static ParamBlockDesc2 ribbon_param_blk(
    0,                              // Block ID
    _M("RibbonObjectParameters"),   // Internal name
    0,                              // Resource ID (none)
    &ribbonObjDesc,                 // ClassDesc2*
    P_AUTO_CONSTRUCT | P_AUTO_UI | P_MULTIMAP | P_HASCATEGORY,

    // P_AUTO_CONSTRUCT
    0,

    // P_MULTIMAP: 2 rollouts (Material first, Properties second)
    MAP_COUNT,
    MAP_MATERIAL,   IDD_ROLLOUT_MATERIAL,   IDS_ROLLOUT_MATERIAL,   0, 0, NULL, ROLLUP_CAT_STANDARD,
    MAP_PROPERTIES, IDD_ROLLOUT_PROPERTIES, IDS_ROLLOUT_PROPERTIES, 0, 0, NULL, ROLLUP_CAT_STANDARD + 1,

    // ===================================================================
    // Rollout: Ribbon Properties
    // ===================================================================

    pb_height_above, _M("Height Above"), TYPE_FLOAT, P_ANIMATABLE, IDS_HEIGHTABOVE,
        p_default,      10.0f,
        p_ms_default,   10.0f,
        p_range,        0.0f, 10000.0f,
        p_ui,           MAP_PROPERTIES, TYPE_SPINNER, EDITTYPE_UNIVERSE,
                        IDC_EDIT_HEIGHTABOVE, IDC_SPIN_HEIGHTABOVE, SPIN_AUTOSCALE,
    p_end,

    pb_height_below, _M("Height Below"), TYPE_FLOAT, P_ANIMATABLE, IDS_HEIGHTBELOW,
        p_default,      10.0f,
        p_ms_default,   10.0f,
        p_range,        0.0f, 10000.0f,
        p_ui,           MAP_PROPERTIES, TYPE_SPINNER, EDITTYPE_UNIVERSE,
                        IDC_EDIT_HEIGHTBELOW, IDC_SPIN_HEIGHTBELOW, SPIN_AUTOSCALE,
    p_end,

    pb_edges_per_second, _M("Edges Per Second"), TYPE_INT, P_ANIMATABLE, IDS_EDGESPERSEC,
        p_default,      10,
        p_ms_default,   10,
        p_range,        0, 1000,
        p_ui,           MAP_PROPERTIES, TYPE_SPINNER, EDITTYPE_INT,
                        IDC_EDIT_EDGESPERSEC, IDC_SPIN_EDGESPERSEC, SPIN_AUTOSCALE,
    p_end,

    pb_edge_lifetime, _M("Edge Lifetime"), TYPE_FLOAT, P_ANIMATABLE, IDS_EDGELIFETIME,
        p_default,      2.0f,
        p_ms_default,   2.0f,
        p_range,        0.001f, 100000.0f,
        p_ui,           MAP_PROPERTIES, TYPE_SPINNER, EDITTYPE_UNIVERSE,
                        IDC_EDIT_EDGELIFETIME, IDC_SPIN_EDGELIFETIME, SPIN_AUTOSCALE,
    p_end,

    pb_color, _M("Color"), TYPE_RGBA, P_ANIMATABLE, IDS_COLOR,
        p_default,      Color(1.0f, 1.0f, 1.0f),
        p_ms_default,   Color(1.0f, 1.0f, 1.0f),
        p_ui,           MAP_PROPERTIES, TYPE_COLORSWATCH, IDC_COLORSWATCH,
    p_end,

    pb_alpha, _M("Alpha"), TYPE_FLOAT, P_ANIMATABLE, IDS_ALPHA,
        p_default,      1.0f,
        p_ms_default,   1.0f,
        p_range,        0.0f, 1.0f,
        p_ui,           MAP_PROPERTIES, TYPE_SPINNER, EDITTYPE_UNIVERSE,
                        IDC_EDIT_ALPHA, IDC_SPIN_ALPHA, SPIN_AUTOSCALE,
    p_end,

    pb_gravity, _M("Gravity"), TYPE_FLOAT, P_ANIMATABLE, IDS_GRAVITY,
        p_default,      0.0f,
        p_ms_default,   0.0f,
        p_range,        -10000.0f, 10000.0f,
        p_ui,           MAP_PROPERTIES, TYPE_SPINNER, EDITTYPE_UNIVERSE,
                        IDC_EDIT_GRAVITY, IDC_SPIN_GRAVITY, SPIN_AUTOSCALE,
    p_end,

    // ===================================================================
    // Rollout: Ribbon Material Parameters
    // ===================================================================

    pb_tex_rows, _M("Texture Rows"), TYPE_INT, 0, IDS_TEXROWS,
        p_default,      1,
        p_ms_default,   1,
        p_range,        1, 100,
        p_ui,           MAP_MATERIAL, TYPE_SPINNER, EDITTYPE_INT,
                        IDC_EDIT_TEXROWS, IDC_SPIN_TEXROWS, SPIN_AUTOSCALE,
    p_end,

    pb_tex_cols, _M("Texture Columns"), TYPE_INT, 0, IDS_TEXCOLS,
        p_default,      1,
        p_ms_default,   1,
        p_range,        1, 100,
        p_ui,           MAP_MATERIAL, TYPE_SPINNER, EDITTYPE_INT,
                        IDC_EDIT_TEXCOLS, IDC_SPIN_TEXCOLS, SPIN_AUTOSCALE,
    p_end,

    pb_tex_slot, _M("Texture Slot"), TYPE_INT, P_ANIMATABLE, IDS_TEXSLOT,
        p_default,      0,
        p_ms_default,   0,
        p_range,        0, 127,
        p_ui,           MAP_MATERIAL, TYPE_SPINNER, EDITTYPE_INT,
                        IDC_EDIT_TEXSLOT, IDC_SPIN_TEXSLOT, SPIN_AUTOSCALE,
    p_end,

    pb_material, _M("Material"), TYPE_MTL, P_SUBANIM, IDS_MATERIAL,
        p_ui,           MAP_MATERIAL, TYPE_MTLBUTTON, IDC_MTLBUTTON,
    p_end,

    p_end   // Final terminator
);

// =========================================================================
// Controller-type combobox helpers
// =========================================================================

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
static const CtrlComboMapping s_ctrlMappings[] = {
    { IDC_CTRL_ABOVE,    pb_height_above    },
    { IDC_CTRL_BELOW,    pb_height_below    },
    { IDC_CTRL_ALPHA,    pb_alpha           },
    { IDC_CTRL_EMISSION, pb_edges_per_second },
    { IDC_CTRL_LIFESPAN, pb_edge_lifetime   },
    { IDC_CTRL_GRAVITY,  pb_gravity         },
};
static const int s_ctrlMappingCount = sizeof(s_ctrlMappings) / sizeof(s_ctrlMappings[0]);

// =========================================================================
// RibbonPropsDlgProc — controller-type comboboxes
// =========================================================================

INT_PTR RibbonPropsDlgProc::DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                                      UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
    {
        IParamBlock2* pb = map->GetParamBlock();
        for (int m = 0; m < s_ctrlMappingCount; m++)
            SetupCtrlCombo(hWnd, s_ctrlMappings[m].comboID, pb, s_ctrlMappings[m].paramID);

        // Color controller combo.
        {
            HWND hCombo = GetDlgItem(hWnd, IDC_CTRL_COLOR);
            if (hCombo) {
                SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
                for (int i = 0; i < s_ctrlTypeCount; i++)
                    SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)s_ctrlTypeNames[i]);

                int tabIndex = pb->GetDesc()->IDtoIndex(pb_color);
                Control* ctrl = pb->GetControllerByIndex(tabIndex);
                int sel = ctrl ? 2 : 0;
                SendMessage(hCombo, CB_SETCURSEL, sel, 0);
            }
        }
        return TRUE;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == CBN_SELCHANGE)
        {
            int loWord = LOWORD(wParam);
            for (int m = 0; m < s_ctrlMappingCount; m++) {
                if (loWord == s_ctrlMappings[m].comboID) {
                    auto sel = SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0);
                    if (sel != CB_ERR)
                        SwitchControllerType(map->GetParamBlock(),
                                             s_ctrlMappings[m].paramID, (int)sel);
                    return TRUE;
                }
            }
            if (loWord == IDC_CTRL_COLOR) {
                auto sel = SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    IParamBlock2* pb = map->GetParamBlock();
                    int tabIndex = pb->GetDesc()->IDtoIndex(pb_color);
                    Control* newCtrl = nullptr;
                    switch (sel) {
                    case 0: pb->SetControllerByIndex(tabIndex, 0, nullptr); break;
                    case 1: newCtrl = static_cast<Control*>(
                                CreateInstance(CTRL_POINT3_CLASS_ID,
                                              Class_ID(LININTERP_POSITION_CLASS_ID, 0)));
                            if (newCtrl) pb->SetControllerByIndex(tabIndex, 0, newCtrl);
                            break;
                    case 2: newCtrl = NewDefaultPoint3Controller();
                            if (newCtrl) pb->SetControllerByIndex(tabIndex, 0, newCtrl);
                            break;
                    case 3: newCtrl = static_cast<Control*>(
                                CreateInstance(CTRL_POINT3_CLASS_ID,
                                              Class_ID(TCBINTERP_POSITION_CLASS_ID, 0)));
                            if (newCtrl) pb->SetControllerByIndex(tabIndex, 0, newCtrl);
                            break;
                    }
                }
                return TRUE;
            }
        }
        break;
    }
    }
    return FALSE;
}

// =========================================================================
// RibbonMatDlgProc — controller-type combobox for Sequence Position
// =========================================================================

INT_PTR RibbonMatDlgProc::DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                                    UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
    {
        IParamBlock2* pb = map->GetParamBlock();
        SetupCtrlCombo(hWnd, IDC_CTRL_TEXSLOT, pb, pb_tex_slot);
        return TRUE;
    }
    case WM_COMMAND:
    {
        if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_CTRL_TEXSLOT)
        {
            auto sel = SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR)
                SwitchControllerType(map->GetParamBlock(), pb_tex_slot, (int)sel);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// =========================================================================
// RibbonObjClassDesc
// =========================================================================

void* RibbonObjClassDesc::Create(BOOL loading)
{
    return new RibbonObject();
}

// =========================================================================
// RibbonObjCreateCallBack
// =========================================================================

int RibbonObjCreateCallBack::proc(ViewExp* vpt, int msg, int point,
                                   int flags, IPoint2 m, Matrix3& mat)
{
    if (msg == MOUSE_FREEMOVE)
        vpt->SnapPreview(m, m);

    if (msg == MOUSE_POINT || msg == MOUSE_MOVE)
    {
        switch (point)
        {
        case 0:
            ob->suspendSnap = 1;
            mat.SetTrans(vpt->SnapPoint(m, m));
            break;
        case 1:
            mat.SetTrans(vpt->SnapPoint(m, m));
            if (msg == MOUSE_POINT)
            {
                ob->suspendSnap = 0;
                return CREATE_STOP;
            }
            break;
        }
    }
    else if (msg == MOUSE_ABORT)
        return CREATE_ABORT;

    return CREATE_CONTINUE;
}

// =========================================================================
// RibbonPostLoadCallback
// =========================================================================

void RibbonPostLoadCallback::proc(ILoad* iload)
{
    // Reserved for post-load refresh.
}

// =========================================================================
// RibbonObject — construction / destruction
// =========================================================================

RibbonObject::RibbonObject()
{
    pblock2 = nullptr;
    suspendSnap = 0;
    buildingTrail = false;
    GetRibbonDesc()->MakeAutoParamBlocks(this);
    SetAFlag(A_PLUGIN1);
}

RibbonObject::~RibbonObject()
{
    DeleteAllRefsFromMe();
}

void RibbonObject::DeleteThis()
{
    delete this;
}

// =========================================================================
// RibbonObject — reference management
// =========================================================================

RefResult RibbonObject::NotifyRefChanged(const Interval& changeInt, RefTargetHandle hTarget,
                                         PartID& partID, RefMessage message, BOOL propagate)
{
    if (message == REFMSG_CHANGE && this == s_editOb)
        InvalidateUI();
    return REF_SUCCEED;
}

RefTargetHandle RibbonObject::Clone(RemapDir& remap)
{
    RibbonObject* newob = new RibbonObject();
    newob->ReplaceReference(0, remap.CloneRef(pblock2));
    BaseClone(this, newob, remap);
    return newob;
}

// =========================================================================
// RibbonObject — object evaluation
// =========================================================================

ObjectState RibbonObject::Eval(TimeValue t)
{
    return ObjectState(this);
}

Interval RibbonObject::ObjectValidity(TimeValue t)
{
    Interval ivalid = FOREVER;
    float    floatdummy;
    int      intdummy;

    pblock2->GetValue(pb_height_above,     t, floatdummy, ivalid);
    pblock2->GetValue(pb_height_below,     t, floatdummy, ivalid);
    pblock2->GetValue(pb_edges_per_second, t, intdummy,   ivalid);
    pblock2->GetValue(pb_edge_lifetime,    t, floatdummy, ivalid);
    pblock2->GetValue(pb_tex_rows,         t, intdummy,   ivalid);
    pblock2->GetValue(pb_tex_cols,         t, intdummy,   ivalid);
    pblock2->GetValue(pb_tex_slot,         t, intdummy,   ivalid);

    Color colordummy;
    pblock2->GetValue(pb_color,            t, colordummy, ivalid);

    return ivalid;
}

// =========================================================================
// RibbonObject — viewport creation
// =========================================================================

CreateMouseCallBack* RibbonObject::GetCreateMouseCallBack()
{
    pointHelpCreateCB.SetObj(this);
    return &pointHelpCreateCB;
}

// =========================================================================
// RibbonObject — type conversion
// =========================================================================

Object* RibbonObject::ConvertToType(TimeValue t, Class_ID obtype)
{
    assert(false);
    return nullptr;
}

Mesh* RibbonObject::GetRenderMesh(TimeValue t, INode* inode, View& view, BOOL& needDelete)
{
    needDelete = FALSE;
    return &mesh;
}

// =========================================================================
// RibbonObject — UI / parameter editing
// =========================================================================

void RibbonObject::BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev)
{
    s_ip     = ip;
    s_editOb = this;
    GetRibbonDesc()->BeginEditParams(ip, this, flags, prev);
    ribbon_param_blk.SetUserDlgProc(MAP_PROPERTIES, new RibbonPropsDlgProc(this));
    ribbon_param_blk.SetUserDlgProc(MAP_MATERIAL,   new RibbonMatDlgProc(this));
}

void RibbonObject::EndEditParams(IObjParam* ip, ULONG flags, Animatable* next)
{
    s_editOb = nullptr;
    s_ip     = nullptr;
    GetRibbonDesc()->EndEditParams(ip, this, flags, next);
    ClearAFlag(A_PLUGIN1);
}

void RibbonObject::InvalidateUI()
{
    ribbon_param_blk.InvalidateUI(pblock2->LastNotifyParamID());
}

// =========================================================================
// RibbonObject — display and hit testing
// =========================================================================

int RibbonObject::Display(TimeValue t, INode* inode, ViewExp* vpt, int flags)
{
    DrawAndHit(t, inode, vpt);
    return 0;
}

int RibbonObject::DrawAndHit(TimeValue t, INode* inode, ViewExp* vpt)
{
    if (!pblock2)
        return 0;

    Interval ivalid = FOREVER;
    float  height_above, height_below, gravity;
    Color  color;

    pblock2->GetValue(pb_height_above, t, height_above, ivalid);
    pblock2->GetValue(pb_height_below, t, height_below, ivalid);
    pblock2->GetValue(pb_gravity,      t, gravity,      ivalid);
    pblock2->GetValue(pb_color,        t, color,        ivalid);

    GraphicsWindow* gw = vpt->getGW();
    int limits = gw->getRndLimits();

    Matrix3 objTM = inode->GetObjectTM(t);
    gw->setTransform(objTM);

    if (inode->Selected())
    {
        gw->setColor(TEXT_COLOR,  GetUIColor(COLOR_SELECTION));
        gw->setColor(LINE_COLOR,  GetUIColor(COLOR_SELECTION));
    }
    else if (!inode->IsFrozen() && !inode->Dependent())
    {
        gw->setColor(TEXT_COLOR,  (Point3)color);
        gw->setColor(LINE_COLOR,  (Point3)color);
    }

    Point3 origin(0.0f, 0.0f, 0.0f);
    gw->marker(&origin, X_MRKR);

    Point3 pts[2];
    pts[0] = Point3(0.0f,  height_above, 0.0f);
    pts[1] = Point3(0.0f, -height_below, 0.0f);
    gw->polyline(2, pts, nullptr, nullptr, FALSE, nullptr);

    if (!suspendSnap)
        BuildEdgeTrail(t, inode);

    if (edgeHistory.size() >= 2)
    {
        Matrix3 identTM(1);
        gw->setTransform(identTM);

        if (inode->Selected())
            gw->setColor(LINE_COLOR, GetUIColor(COLOR_SELECTION));
        else if (!inode->IsFrozen() && !inode->Dependent())
            gw->setColor(LINE_COLOR, (Point3)color);

        bool useObjColor = !inode->Selected() && !inode->IsFrozen() && !inode->Dependent();

        Point3 prevTop, prevBot, prevDir;
        bool hasPrev = false;
        constexpr int kHermiteSubs = 4;
        for (const auto& edge : edgeHistory)
        {
            if (useObjColor)
            {
                Color edgeColor;
                Interval civld = FOREVER;
                pblock2->GetValue(pb_color, edge.birthTime, edgeColor, civld);
                gw->setColor(LINE_COLOR, (Point3)edgeColor);
            }

            Point3 curTop, curBot, curDir;
            ComputeEdgeWorldPos(inode, pblock2, edge, t, gravity, curTop, curBot, &curDir);

            pts[0] = curTop;
            pts[1] = curBot;
            gw->polyline(2, pts, nullptr, nullptr, FALSE, nullptr);

            if (hasPrev)
            {
                // Hermite-like interpolation matching engine's InterpEdge tangent blending.
                // Engine formula: pos(t) = p0*(1-t) + p1*t + t*(1-t)*(prevDirScaled - currDirScaled)
                // Plugin iterates newest-to-oldest, so prev=newer, cur=older.
                // (curDir - prevDir) matches engine's (olderDir - newerDir) convention.
                // Note: engine uses birth positions for scale; midpoints here include gravity
                // offset which is a minor approximation.
                Point3 midPrev = (prevTop + prevBot) * 0.5f;
                Point3 midCurr = (curTop + curBot) * 0.5f;
                float scale = Length(midCurr - midPrev);
                Point3 tangentDiff = (curDir - prevDir) * scale;

                Point3 pTop = prevTop, pBot = prevBot;
                for (int s = 1; s <= kHermiteSubs; s++)
                {
                    float st = (float)s / (float)kHermiteSubs;
                    Point3 correction = tangentDiff * (st * (1.0f - st));
                    Point3 cTop = prevTop + (curTop - prevTop) * st + correction;
                    Point3 cBot = prevBot + (curBot - prevBot) * st + correction;

                    pts[0] = pTop;
                    pts[1] = cTop;
                    gw->polyline(2, pts, nullptr, nullptr, FALSE, nullptr);
                    pts[0] = pBot;
                    pts[1] = cBot;
                    gw->polyline(2, pts, nullptr, nullptr, FALSE, nullptr);

                    pTop = cTop;
                    pBot = cBot;
                }
            }

            prevTop = curTop;
            prevBot = curBot;
            prevDir = curDir;
            hasPrev = true;
        }
    }

    gw->setRndLimits(limits);
    return 1;
}

// =========================================================================
// RibbonObject — edge trail computation
// =========================================================================

void RibbonObject::BuildEdgeTrail(TimeValue t, INode* inode)
{
    if (buildingTrail || !pblock2 || !inode)
        return;
    buildingTrail = true;

    edgeHistory.clear();

    Interval ivalid = FOREVER;
    float  edge_lifetime;
    int    edges_per_second;

    pblock2->GetValue(pb_edges_per_second, t, edges_per_second, ivalid);
    pblock2->GetValue(pb_edge_lifetime,    t, edge_lifetime,    ivalid);

    if (edges_per_second <= 0 || edge_lifetime <= 0.0f)
    {
        buildingTrail = false;
        return;
    }

    int tickInterval = (int)(kTicksPerSec / (float)edges_per_second);
    if (tickInterval < 1) tickInterval = 1;

    TimeValue lifetimeTicks = (TimeValue)(edge_lifetime * kTicksPerSec);
    TimeValue startAnim = GetCOREInterface()->GetAnimRange().Start();

    int maxEdges = (int)ceilf((float)edges_per_second * edge_lifetime) + 2;
    if (maxEdges < 2) maxEdges = 2;

    for (int i = 0; i < maxEdges; i++)
    {
        TimeValue birthTime = t - i * tickInterval;
        if (birthTime < startAnim)
            break;
        if ((t - birthTime) > lifetimeTicks)
            break;

        if (inode->IsNodeHidden() || inode->GetVisibility(birthTime) <= 0.0f)
            break;

        RibbonEdge edge;
        edge.birthTime = birthTime;
        edgeHistory.push_back(edge);
    }

    buildingTrail = false;
}

// =========================================================================
// RibbonObject — hit testing
// =========================================================================

int RibbonObject::HitTest(TimeValue t, INode* inode, int type, int crossing,
                           int flags, IPoint2* p, ViewExp* vpt)
{
    Matrix3 tm;
    HitRegion hitRegion;

    GraphicsWindow* gw = vpt->getGW();

    gw->setTransform(tm);
    tm = inode->GetObjectTM(t);

    MakeHitRegion(hitRegion, type, crossing, 4, p);
    DWORD savedLimits = gw->getRndLimits();
    gw->setRndLimits((savedLimits & ~GW_BACKCULL) | GW_PICK);
    gw->setHitRegion(&hitRegion);
    gw->clearHitCode();

    DrawAndHit(t, inode, vpt);

    gw->setRndLimits(savedLimits);

    if (hitRegion.type == POINT_RGN || hitRegion.crossing)
        return gw->checkHitCode();

    return TRUE;
}

void RibbonObject::Snap(TimeValue t, INode* inode, SnapInfo* snap, IPoint2* p, ViewExp* vpt)
{
    if (suspendSnap)
        return;

    Matrix3 tm = inode->GetObjectTM(t);
    GraphicsWindow* gw = vpt->getGW();
    gw->setTransform(tm);

    Matrix3 invPlane = Inverse(snap->plane);

    if (snap->vertPriority > 0 && snap->vertPriority <= snap->priority)
    {
        Point2 fp((float)p->x, (float)p->y);
        Point2 screen2;
        IPoint3 pt3;
        Point3 thePoint(0.0f, 0.0f, 0.0f);

        if (snap->snapType != SNAP_2D ||
            (snap->flags & SNAP_IN_PLANE) ||
            fabs((thePoint * tm * invPlane).z) <= 0.0001f)
        {
            gw->wTransPoint(&thePoint, &pt3);

            screen2.x = (float)pt3.x;
            screen2.y = (float)pt3.y;

            int len = (int)Length(screen2 - fp);

            if (len <= snap->strength)
            {
                snap->priority  = snap->vertPriority;
                snap->bestWorld = thePoint * tm;
                snap->bestScreen = screen2;
                snap->bestDist  = len;
            }
        }
    }
}

void RibbonObject::GetWorldBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box)
{
    Matrix3 tm = inode->GetObjectTM(t);
    Interval ivalid = FOREVER;
    float height_above, height_below, gravity;

    pblock2->GetValue(pb_height_above, t, height_above, ivalid);
    pblock2->GetValue(pb_height_below, t, height_below, ivalid);
    pblock2->GetValue(pb_gravity,      t, gravity,      ivalid);

    Point3 pos = tm.GetTrans();
    Point3 localY = Normalize(tm.GetRow(1));
    box = Box3(pos + localY * height_above, pos - localY * height_below);

    if (edgeHistory.empty())
        BuildEdgeTrail(t, inode);

    for (const auto& edge : edgeHistory)
    {
        Point3 eTop, eBot;
        ComputeEdgeWorldPos(inode, pblock2, edge, t, gravity, eTop, eBot);

        box += eTop;
        box += eBot;
    }

    box.EnlargeBy(10.0f);
}

void RibbonObject::GetLocalBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box)
{
    Interval ivalid = FOREVER;
    float size_above, size_below;

    pblock2->GetValue(pb_height_above, t, size_above, ivalid);
    pblock2->GetValue(pb_height_below, t, size_below, ivalid);

    box = Box3(Point3(0.0f, -size_below, 0.0f), Point3(0.0f, size_above, 0.0f));
    box.EnlargeBy(10.0f);
}

// =========================================================================
// RibbonObject — edge world-position helper
// =========================================================================

void RibbonObject::ComputeEdgeWorldPos(INode* inode, IParamBlock2* pb,
                                       const RibbonEdge& edge, TimeValue t,
                                       float gravity, Point3& outTop, Point3& outBot,
                                       Point3* outDir)
{
    Interval hivalid = FOREVER;
    float ha, hb;
    pb->GetValue(pb_height_above, edge.birthTime, ha, hivalid);
    pb->GetValue(pb_height_below, edge.birthTime, hb, hivalid);

    Matrix3 edgeTM = inode->GetObjectTM(edge.birthTime);
    Point3 pos    = edgeTM.GetTrans();
    Point3 localY = Normalize(edgeTM.GetRow(1));
    outTop = pos + localY * ha;
    outBot = pos - localY * hb;

    float ageSec = (float)(t - edge.birthTime) / kTicksPerSec;
    float gravOffset = -gravity * ageSec * ageSec;
    Point3 gravVec(0.0f, 0.0f, gravOffset);
    outTop += gravVec;
    outBot += gravVec;

    if (outDir)
        *outDir = Normalize(edgeTM.GetRow(2));
}

// =========================================================================
// RibbonObject — serialization
// =========================================================================

IOResult RibbonObject::Load(ILoad* iload)
{
    iload->RegisterPostLoadCallback(new RibbonPostLoadCallback(this));
    return IO_OK;
}
