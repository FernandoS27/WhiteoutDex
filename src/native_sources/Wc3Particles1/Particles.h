/**
 * @file Particles.h
 * @brief Class declarations for the Wc3Particles1 3ds Max particle plugin.
 *
 * Wc3Particles1 is a spherical/cone model-particle emitter for Autodesk
 * 3ds Max 2016+. Based on the Warcraft III CParticleEmitter (ParticleEmitter1),
 * which spawns sub-model instances with cone-shaped velocity distribution
 * defined by latitude and longitude angles.
 *
 * Unlike ParticleEmitter2 (Wc3Particles2), this emitter:
 * - Emits from a point (no rectangular emitter plane)
 * - Uses latitude/longitude cone angles for emission direction
 * - Has a single scale value (no start/mid/end keyframing)
 * - References a model file instead of a texture
 * - Has no billboard/tail/UV animation support
 *
 * Plugin identity:
 * - Class_ID:      {0x12E4F5A6, 0x3B7C8D9E}
 * - SuperClass_ID: GEOMOBJECT_CLASS_ID
 */

#pragma once

#include "resource.h"

// 3ds Max SDK
#include <max.h>
#include <maxversion.h>
#include <simpobj.h>
#include <particle.h>
#include <iparamb2.h>
#include <iparamm2.h>
#include <custcont.h>
#include <maxscript/maxscript.h>
/// Permutation table lookup, exported from core.dll.
CoreExport int Perm(int v);

// ---------- SDK version compatibility shims ----------
#if MAX_PRODUCT_YEAR_NUMBER >= 2018
  #define SUBANIM_NAME_SIG(i)        MSTR SubAnimName(int i, bool localized = true) override
  #define GET_PARAM_NAME_SIG(idx)    MSTR GetParameterName(int idx, bool localized = true) override
  #define GET_OBJECT_NAME_SIG        const MCHAR* GetObjectName(bool localized = true) const override
  #define NONLOCALIZED_CLASSNAME_DECL  const MCHAR* NonLocalizedClassName() override;
#else
  #define SUBANIM_NAME_SIG(i)        MSTR SubAnimName(int i) override
  #define GET_PARAM_NAME_SIG(idx)    MSTR GetParameterName(int idx) override
  #define GET_OBJECT_NAME_SIG        const MCHAR* GetObjectName() override
  #define NONLOCALIZED_CLASSNAME_DECL  /* not available before Max 2018 */
#endif

// Standard library
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

// Windows
#include <commctrl.h>
#include <commdlg.h>
#include <winnetwk.h>

/// Unique Class_ID registered with 3ds Max for this plugin.
const auto WC3PARTICLES1_CLASS_ID = Class_ID(0x12E4F5A6, 0x3B7C8D9E);

/// ParamBlock2 block identifiers.
enum : BlockID { wc3particles1_params };

/// Rollout map identifiers for P_MULTIMAP.
enum Wc3Particles1MapID {
    P1_MAP_CONFIG = 0,  ///< Import/Export config rollout.
    P1_MAP_EMITTER,     ///< Emitter Options rollout.
    P1_MAP_TIMING,      ///< Timing Options rollout.
    P1_MAP_MODEL,       ///< Model Options rollout.
    P1_MAP_COUNT        ///< Total number of rollouts.
};

/**
 * @brief Parameter indices within the Wc3Particles1 ParamBlock2.
 *
 * Matches the CParticleEmitter fields from the engine pseudocode.
 */
enum P1ParamBlockIndex : ParamID {
    P1_PB_COUNT          = 0,  ///< Maximum live-particle count (auto-computed).
    P1_PB_SPEED          = 1,  ///< Initial emission speed / velocity (TYPE_FLOAT, animatable).
    P1_PB_EMISSION_RATE  = 2,  ///< Particle emission rate in particles/sec (TYPE_FLOAT, animatable).
    P1_PB_LIFE           = 3,  ///< Per-particle lifetime in seconds (TYPE_FLOAT).
    P1_PB_ACCELERATION   = 4,  ///< Gravity acceleration along -Z (TYPE_FLOAT, animatable).
    P1_PB_LATITUDE       = 5,  ///< Emission cone latitude half-angle in degrees (TYPE_FLOAT, animatable).
    P1_PB_LONGITUDE      = 6,  ///< Emission cone longitude half-angle in degrees (TYPE_FLOAT, animatable).
    P1_PB_SCALE          = 7,  ///< Particle scale (TYPE_FLOAT).
};

constexpr int P1_PARAM_COUNT_MAX = 500;  ///< Hard cap on live particle count.
constexpr int P1_NUM_PARAMS      = 8;    ///< Total number of parameters.
constexpr int P1_TICKS_PER_SEC   = 4800; ///< 3ds Max ticks per second.

/// Interface ID for cross-plugin model path query via Animatable::GetInterface(ULONG).
/// Returns a pointer to the internal m_modelPath MSTR, or nullptr for other objects.
constexpr ULONG WC3P1_MODEL_PATH_IID = 0x7B3C8D01;

/// @cond FORWARD_DECLS
class GenParticle1;
class Wc3Particles1Particle;
class Wc3Particles1ClassDesc;
class Wc3Particles1ParticleDraw;
class Emitter1CreateCallback;
class Wc3Particles1DlgProc;
/// @endcond

/**
 * @brief Base particle system object for the model emitter.
 *
 * Manages parameter block lifetime, particle birth/update/death simulation,
 * emitter mesh construction, viewport display, hit‐testing, and serialisation.
 */
class GenParticle1 : public SimpleParticle {
public:
    MSTR                m_modelPath;        ///< Model filename.
    MSTR                m_modelPrefix;      ///< Model path prefix.
    int                 stepSize = 0;       ///< Integration step size in ticks.
    IParamBlock2*       pblock2 = nullptr;  ///< Owned ParamBlock2 reference (ref index 0).
    std::mt19937        m_rng;              ///< PRNG for deterministic particle birth.

    explicit GenParticle1();
    ~GenParticle1() override;

    /// @name Particle core
    /// @{
    void    BirthParticle(INode* node, TimeValue bt, int index);
    void    ComputeParticleStart(TimeValue t0, INode* node);
    int     CountLive();
    int     GetParticleCount();
    /// @}

    /// @name 3ds Max UI callbacks
    /// @{
    void    BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override;
    void    EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override;
    /// @}

    /// @name Serialisation
    /// @{
    IOResult Save(ISave* isave) override;
    IOResult Load(ILoad* iload) override;
    /// @}

    /// @name Cross-plugin interface
    /// @{
    void*   GetInterface(ULONG id) override;
    /// @}

    /// @name Reference management
    /// @{
    int     NumRefs() override;
    RefTargetHandle GetReference(int i) override;
    void    SetReference(int i, RefTargetHandle rtarg) override;
    int     NumSubs() override;
    Animatable* SubAnim(int i) override;
    SUBANIM_NAME_SIG(i);
    int     NumParamBlocks() override;
    IParamBlock2* GetParamBlock(int i) override;
    IParamBlock2* GetParamBlockByID(BlockID id) override;
    RefResult NotifyRefChanged(const Interval& changeInt, RefTargetHandle hTarget,
                               PartID& partID, RefMessage message, BOOL propagate) override;
    /// @}

    /// @name Bounding box
    /// @{
    void    GetLocalBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box) override;
    void    GetWorldBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box) override;
    /// @}

    /// @name Viewport display
    /// @{
    int     Display(TimeValue t, INode* inode, ViewExp* vpt, int flags) override;
    int     HitTest(TimeValue t, INode* inode, int type, int crossing,
                    int flags, IPoint2* p, ViewExp* vpt) override;
    /// @}

    /// @name SimpleParticle virtuals
    /// @{
    void    UpdateParticles(TimeValue t, INode* node) override;
    void    BuildEmitter(TimeValue t, Mesh& mesh) override;
    Interval GetValidity(TimeValue t) override;
    void    InvalidateUI() override;
    ParamDimension* GetParameterDim(int pbIndex) override;
    GET_PARAM_NAME_SIG(pbIndex);
    Point3  ParticlePosition(TimeValue t, int i) override;
    Point3  ParticleVelocity(TimeValue t, int i) override;
    int     ParticleLife(TimeValue t, int i) override;
    int     RenderEnd(TimeValue t) override;
    void    RescaleWorldUnits(float f) override;
    CreateMouseCallBack* GetCreateMouseCallBack() override;
    void    MapKeys(TimeMap* map, DWORD flags) override;
    int     RenderBegin(TimeValue t, ULONG flags) override;
    void    DeleteThis() override;
    /// @}
};

/**
 * @brief Concrete 3ds Max plugin class registered under WC3PARTICLES1_CLASS_ID.
 */
class Wc3Particles1Particle : public GenParticle1 {
public:
    Wc3Particles1Particle();
    ~Wc3Particles1Particle() override;

    Class_ID    ClassID() override;
    GET_OBJECT_NAME_SIG;
    BOOL        IsInstanceDependent() override;

    float       ParticleSize(TimeValue t, int i) override;
    int         ParticleCenter(TimeValue t, int i) override;
    MarkerType  GetMarkerType() override;

    Mesh*       GetRenderMesh(TimeValue t, INode* inode, View& view, BOOL& needDelete) override;
    ReferenceTarget* Clone(RemapDir& remap) override;
};

/**
 * @brief Custom per-particle viewport renderer.
 *
 * Draws each particle as a small wireframe diamond, scaled by the emitter's
 * single scale parameter.
 */
class Wc3Particles1ParticleDraw : public CustomParticleDisplay {
public:
    float       scale = 1.0f;  ///< Particle scale from PB_SCALE.

    Wc3Particles1ParticleDraw() = default;
    BOOL DrawParticle(GraphicsWindow* gw, ParticleSys& parts, int i) override;
};

/**
 * @brief Plugin ClassDesc2 singleton for Wc3Particles1.
 */
class Wc3Particles1ClassDesc : public ClassDesc2 {
public:
    int             IsPublic() override;
    void*           Create(BOOL loading) override;
    const TCHAR*    ClassName() override;
    NONLOCALIZED_CLASSNAME_DECL
    SClass_ID       SuperClassID() override;
    Class_ID        ClassID() override;
    const TCHAR*    Category() override;
    const TCHAR*    InternalName() override;
    HINSTANCE       HInstance() override;
};

/**
 * @brief Viewport mouse-creation callback.
 *
 * Single-click creation: the emitter is a point source, so one click places it.
 */
class Emitter1CreateCallback : public CreateMouseCallBack {
public:
    GenParticle1*   obj = nullptr;
    int proc(ViewExp* vpt, int msg, int point, int flags,
             IPoint2 m, Matrix3& mat) override;
};

/**
 * @brief ParamMap2 dialog callback for the Emitter and Timing rollouts.
 */
class Wc3Particles1DlgProc : public ParamMap2UserDlgProc {
public:
    GenParticle1* po;
    explicit Wc3Particles1DlgProc(GenParticle1* p) : po(p) {}
    ~Wc3Particles1DlgProc() override = default;
    INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                    UINT msg, WPARAM wParam, LPARAM lParam) override;
    void    DeleteThis() override { delete this; }
    void    SetupMaxRate(HWND hWnd, IParamBlock2* pb, TimeValue t);
    void    BrowseForModelFile(HWND hWnd);
};

/**
 * @brief DlgProc for the Import/Export configuration rollout.
 */
class Config1DlgProc : public ParamMap2UserDlgProc {
public:
    GenParticle1* po;
    BOOL loadDynamic;
    explicit Config1DlgProc(GenParticle1* p) : po(p), loadDynamic(TRUE) {}
    ~Config1DlgProc() override = default;
    INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                    UINT msg, WPARAM wParam, LPARAM lParam) override;
    void    DeleteThis() override { delete this; }
    void    ExportConfig(HWND hWnd, IParamBlock2* pb, TimeValue t);
    void    ImportConfig(HWND hWnd, IParamBlock2* pb, TimeValue t);
};

/// @name Free utility functions
/// @{
const MCHAR*    GetString(UINT id);
ClassDesc2*     GetWc3Particles1Desc();
void            ParticleCacheData(ParticleSys* dst, ParticleSys* src);
BOOL            Parity(Matrix3* tm);
void            FlipAllMeshFaces(Mesh* mesh);
/// @}

// ============================================================================
// Globals
// ============================================================================
extern HINSTANCE    hInstance;
