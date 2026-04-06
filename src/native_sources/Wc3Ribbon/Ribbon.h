/**
 * @file    Ribbon.h
 * @brief   Wc3Ribbon — Ribbon emitter particle system plugin for 3ds Max.
 * @note    Requires 3ds Max 2016+ SDK (x64, Unicode, ParamBlock2).
 */

#pragma once

#include <max.h>
#include <maxversion.h>
#include <iparamb2.h>
#include <iparamm2.h>
#include <simpobj.h>
#include <mesh.h>
#include <deque>

// ---------- SDK version compatibility shims ----------
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
  #define RIBBON_GET_OBJECT_NAME_SIG    const MCHAR* GetObjectName(bool localized = true) const override
  #define RIBBON_NONLOCALIZED_CLASSNAME const MCHAR* NonLocalizedClassName() override { return _M("Wc3Ribbon"); }
#else
  #define RIBBON_GET_OBJECT_NAME_SIG    const MCHAR* GetObjectName() override
  #define RIBBON_NONLOCALIZED_CLASSNAME /* not available before Max 2022 */
#endif

#include "resource.h"

constexpr float kTicksPerSec = 4800.0f;

struct RibbonEdge {
    TimeValue birthTime;
};

extern HINSTANCE hInstance;

const auto RIBBON_CLASS_ID = Class_ID(0x937AA064, 0x9EFFA3DA);

enum : ParamID {
    pb_height_above     = 0,
    pb_height_below     = 1,
    pb_edges_per_second = 2,
    pb_edge_lifetime    = 3,
    pb_tex_rows         = 4,
    pb_tex_cols         = 5,
    pb_tex_slot         = 6,
    pb_material         = 7,
    pb_color            = 8,
    pb_alpha            = 9,
    pb_gravity          = 10,
};

class RibbonObject;

extern ClassDesc2* GetRibbonDesc();
extern const MCHAR* GetString(UINT id);

class RibbonObjClassDesc : public ClassDesc2 {
public:
    int           IsPublic() override                    { return TRUE; }
    void*         Create(BOOL loading = FALSE) override;
    const MCHAR*  ClassName() override                   { return GetString(IDS_CLASSNAME); }
    RIBBON_NONLOCALIZED_CLASSNAME
    SClass_ID     SuperClassID() override                { return GEOMOBJECT_CLASS_ID; }
    Class_ID      ClassID() override                     { return RIBBON_CLASS_ID; }
    const MCHAR*  Category() override                    { return GetString(IDS_CATEGORY); }
    const MCHAR*  InternalName() override                { return _M("RibbonObj"); }
    HINSTANCE     HInstance() override                   { return hInstance; }
};

class RibbonObjCreateCallBack : public CreateMouseCallBack {
public:
    RibbonObject* ob;
    int proc(ViewExp* vpt, int msg, int point, int flags,
             IPoint2 m, Matrix3& mat) override;
    void SetObj(RibbonObject* obj) { ob = obj; }
};

class RibbonPostLoadCallback : public PostLoadCallback {
public:
    RibbonObject* pobj;
    explicit RibbonPostLoadCallback(RibbonObject* p) : pobj(p) {}
    void proc(ILoad* iload) override;
};

class RibbonPropsDlgProc : public ParamMap2UserDlgProc {
public:
    RibbonObject* po;
    explicit RibbonPropsDlgProc(RibbonObject* p) : po(p) {}
    ~RibbonPropsDlgProc() override = default;
    INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                    UINT msg, WPARAM wParam, LPARAM lParam) override;
    void DeleteThis() override { delete this; }
};

class RibbonMatDlgProc : public ParamMap2UserDlgProc {
public:
    RibbonObject* po;
    explicit RibbonMatDlgProc(RibbonObject* p) : po(p) {}
    ~RibbonMatDlgProc() override = default;
    INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                    UINT msg, WPARAM wParam, LPARAM lParam) override;
    void DeleteThis() override { delete this; }
};

class RibbonObject : public GeomObject {
public:
    IParamBlock2* pblock2;
    Mesh          mesh;
    int           suspendSnap;
    int           extDispFlags;
    std::deque<RibbonEdge> edgeHistory;
    bool          buildingTrail;

    static IObjParam*    s_ip;
    static RibbonObject* s_editOb;

    RibbonObject();
    ~RibbonObject();
    void DeleteThis() override;

    Class_ID ClassID() override                              { return RIBBON_CLASS_ID; }

    int    NumSubs() override                                { return 1; }
    Animatable* SubAnim(int i) override                      { return pblock2; }
    int    NumRefs() override                                { return 1; }
    RefTargetHandle GetReference(int i) override             { return pblock2; }
    void   SetReference(int i, RefTargetHandle rtarg) override { pblock2 = (IParamBlock2*)rtarg; }
    RefResult NotifyRefChanged(const Interval& changeInt, RefTargetHandle hTarget,
                               PartID& partID, RefMessage message, BOOL propagate) override;
    RefTargetHandle Clone(RemapDir& remap) override;

    int            NumParamBlocks() override                 { return 1; }
    IParamBlock2*  GetParamBlock(int i) override             { return pblock2; }
    IParamBlock2*  GetParamBlockByID(BlockID id) override    { return (id == 0) ? pblock2 : nullptr; }

    ObjectState Eval(TimeValue t) override;
    Interval    ObjectValidity(TimeValue t) override;
    CreateMouseCallBack* GetCreateMouseCallBack() override;

    int         CanConvertToType(Class_ID obtype) override   { return FALSE; }
    Object*     ConvertToType(TimeValue t, Class_ID obtype) override;
    BOOL        UsesWireColor() override                     { return TRUE; }
    int         DoOwnSelectHilite() override                 { return TRUE; }

    int         IsRenderable() override                      { return FALSE; }
    Mesh*       GetRenderMesh(TimeValue t, INode* inode, View& view, BOOL& needDelete) override;

    RIBBON_GET_OBJECT_NAME_SIG { return GetString(IDS_CLASSNAME); }
    void        InitNodeName(MSTR& s) override               { s = GetString(IDS_CLASSNAME); }

    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev = nullptr) override;
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next = nullptr) override;
    void InvalidateUI();

    void SetExtendedDisplay(int flags) override              { extDispFlags = flags; }

    int  Display(TimeValue t, INode* inode, ViewExp* vpt, int flags) override;
    int  HitTest(TimeValue t, INode* inode, int type, int crossing,
                 int flags, IPoint2* p, ViewExp* vpt) override;
    void Snap(TimeValue t, INode* inode, SnapInfo* snap, IPoint2* p, ViewExp* vpt) override;
    void GetWorldBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box) override;
    void GetLocalBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box) override;

    IOResult Load(ILoad* iload) override;

    int  DrawAndHit(TimeValue t, INode* inode, ViewExp* vpt);
    void BuildEdgeTrail(TimeValue t, INode* inode);
    static void ComputeEdgeWorldPos(INode* inode, IParamBlock2* pb,
                                    const RibbonEdge& edge, TimeValue t,
                                    float gravity, Point3& outTop, Point3& outBot,
                                    Point3* outDir = nullptr);
};
