/**
 * @file Particles.h
 * @brief Class declarations for the Wc3Particles2 3ds Max particle plugin.
 *
 * Wc3Particles2 is a configurable billboard and ribbon particle emitter for
 * Autodesk 3ds Max 2016+. Particles support three-keyframe animated colour,
 * alpha and scale, UV-sheet animation for head and tail components, and five
 * render modes (Blend, Add, Modulate, Mod2X, AlphaKey).
 *
 * Plugin identity:
 * - Class_ID:      {0xD9F33BC9, 0x7A0DA37A}
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
/// Forward-declared here to avoid the texutil.h/fmin conflict with MSVC.
CoreExport int Perm(int v);

// ---------- SDK version compatibility shims ----------
// MAX_PRODUCT_YEAR_NUMBER is defined in maxversion.h (2016 = 2016, 2017 = 2017, etc.).
// In Max 2022 the SDK sealed the old no-localized overloads and introduced new
// 'bool localized' variants that plugins must override instead.
// NonLocalizedClassName() was also added to ClassDesc2 (pure virtual) in Max 2022.
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
  #define SUBANIM_NAME_SIG(i)        MSTR SubAnimName(int i, bool localized = true) override
  #define GET_PARAM_NAME_SIG(idx)    MSTR GetParameterName(int idx, bool localized = true) override
  #define GET_OBJECT_NAME_SIG        const MCHAR* GetObjectName(bool localized = true) const override
  #define NONLOCALIZED_CLASSNAME_DECL  const MCHAR* NonLocalizedClassName() override;
#else
  #define SUBANIM_NAME_SIG(i)        MSTR SubAnimName(int i) override
  #define GET_PARAM_NAME_SIG(idx)    MSTR GetParameterName(int idx) override
  #define GET_OBJECT_NAME_SIG        const MCHAR* GetObjectName() override
  #define NONLOCALIZED_CLASSNAME_DECL  /* not available before Max 2022 */
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
const auto WC3PARTICLES2_CLASS_ID = Class_ID(0xD9F33BC9, 0x7A0DA37A);

/// Interface IDs for cross-DLL access to texture path members.
constexpr ULONG WC3P2_TEXTURE_PATH_IID   = 0x7B3C8D10;  ///< Returns MSTR* to m_particlePath
constexpr ULONG WC3P2_TEXTURE_PREFIX_IID = 0x7B3C8D11;  ///< Returns MSTR* to m_texturePrefix

/// ParamBlock2 block identifiers.
enum : BlockID { wc3particles2_params };

/// Rollout map identifiers for P_MULTIMAP.
enum Wc3Particles2MapID {
    MAP_CONFIG = 0,     ///< Import/Export config rollout.
    MAP_TEXTURE,        ///< Texture Options rollout.
    MAP_EMITTER,        ///< Emitter Options rollout.
    MAP_TIMING,         ///< Timing Options rollout.
    MAP_SIZE,           ///< Size Options rollout.
    MAP_PARTICLE,       ///< Particle Options rollout.
    MAP_OTHER,          ///< Other Options rollout.
    MAP_COUNT           ///< Total number of rollouts.
};

/**
 * @brief Parameter indices within the Wc3Particles2 ParamBlock2.
 *
 * Each enumerator maps directly to the corresponding PB2 parameter ID.
 * The order must match the @c wc3particles2_param_blk descriptor table.
 */
enum ParamBlockIndex : ParamID {
    PB_COUNT        = 0,    ///< Maximum live-particle count (TYPE_INT, clamped to 500).
    PB_SPEED        = 1,    ///< Initial emission speed (TYPE_FLOAT, animatable).
    PB_VARIATION    = 2,    ///< Speed variation as a percentage (TYPE_FLOAT, animatable).
    PB_LIFE         = 3,    ///< Per-particle lifetime in seconds (TYPE_FLOAT).
    PB_WIDTH        = 4,    ///< Emitter rectangle width in world units (TYPE_FLOAT, animatable).
    PB_HEIGHT       = 5,    ///< Emitter rectangle height in world units (TYPE_FLOAT, animatable).
    PB_INITVEL      = 6,    ///< Particle emission rate in particles-per-second (TYPE_FLOAT, animatable).
    PB_ANGLE_Y      = 7,    ///< Half-angle of the emission cone in degrees (TYPE_FLOAT, animatable).
    PB_MIDTIME      = 8,    ///< Normalised time [0.05, 0.95] for the colour/scale mid-keyframe (TYPE_FLOAT).
    PB_COLOR_START  = 9,    ///< Particle colour at birth (TYPE_POINT3, RGB [0,1]).
    PB_COLOR_MID    = 10,   ///< Particle colour at mid-life (TYPE_POINT3, RGB [0,1]).
    PB_COLOR_END    = 11,   ///< Particle colour at death (TYPE_POINT3, RGB [0,1]).
    PB_ALPHA_START        = 12,   ///< Alpha at birth [0, 255] (TYPE_INT).
    PB_ALPHA_MID          = 13,   ///< Alpha at mid-life [0, 255] (TYPE_INT).
    PB_ALPHA_END          = 14,   ///< Alpha at death [0, 255] (TYPE_INT).
    PB_SCALE_START        = 15,   ///< Particle scale at birth (TYPE_FLOAT).
    PB_SCALE_MID          = 16,   ///< Particle scale at mid-life (TYPE_FLOAT).
    PB_SCALE_END          = 17,   ///< Particle scale at death (TYPE_FLOAT).
    PB_HEAD_LIFE_START    = 18,   ///< Head UV animation: first frame during lifespan phase (TYPE_INT).
    PB_HEAD_LIFE_REPEAT   = 19,   ///< Head UV animation: repeat count during lifespan phase (TYPE_INT).
    PB_HEAD_LIFE_END      = 20,   ///< Head UV animation: last frame during lifespan phase (TYPE_INT).
    PB_HEAD_DECAY_START   = 21,   ///< Head UV animation: first frame during decay phase (TYPE_INT).
    PB_HEAD_DECAY_REPEAT  = 22,   ///< Head UV animation: repeat count during decay phase (TYPE_INT).
    PB_HEAD_DECAY_END     = 23,   ///< Head UV animation: last frame during decay phase (TYPE_INT).
    PB_TAIL_LEN     = 24,   ///< Tail streak length multiplier (TYPE_FLOAT).
    PB_TYPE         = 25,   ///< Render mode: 0=Head only, 1=Tail only, 2=Both (TYPE_INT).
    PB_ROWS         = 26,   ///< Number of rows in the UV animation sheet [1, 16] (TYPE_INT).
    PB_COLS         = 27,   ///< Number of columns in the UV animation sheet [1, 16] (TYPE_INT).
    PB_TAIL_LIFE_START    = 28,   ///< Tail UV animation: first frame during lifespan phase (TYPE_INT).
    PB_TAIL_LIFE_REPEAT   = 29,   ///< Tail UV animation: repeat count during lifespan phase (TYPE_INT).
    PB_TAIL_LIFE_END      = 30,   ///< Tail UV animation: last frame during lifespan phase (TYPE_INT).
    PB_TAIL_DECAY_START   = 31,   ///< Tail UV animation: first frame during decay phase (TYPE_INT).
    PB_TAIL_DECAY_REPEAT  = 32,   ///< Tail UV animation: repeat count during decay phase (TYPE_INT).
    PB_TAIL_DECAY_END     = 33,   ///< Tail UV animation: last frame during decay phase (TYPE_INT).
    PB_SQUIRT       = 34,   ///< Squirt mode: burst all particles at emission-rate keyframe transitions (TYPE_INT).
    PB_BLEND        = 35,   ///< Blend mode index: 0=Blend, 1=Add, 2=Modulate, 3=Mod2X, 4=AlphaKey (TYPE_INT).
    PB_GRAVITY      = 36,   ///< Downward gravity acceleration (TYPE_FLOAT, animatable).
    PB_SORT         = 37,   ///< Enable depth-sorted particle rendering (TYPE_INT).
    PB_LINE_EMIT    = 38,   ///< Emit along the full width of the emitter rectangle (TYPE_INT).
    PB_UNSHADED     = 39,   ///< Disable lighting on this emitter's particles (TYPE_INT).
    PB_LATITUDE     = 40,   ///< Deprecated — kept for file compat. Use PB_ANGLE_Y (TYPE_FLOAT, invisible).
    PB_PRIORITY     = 41,   ///< Render priority plane [-100, 100] (TYPE_INT).
    PB_UNFOGGED     = 42,   ///< Disable distance fog on this emitter (TYPE_INT).
    PB_MODELSPACE   = 43,   ///< Simulate particles in emitter-local space rather than world space (TYPE_INT).
    PB_XYQUAD       = 44,   ///< Align quads to the XY plane instead of facing the camera (TYPE_INT).
    PB_REPLACEABLE_ID = 45,  ///< Replaceable texture slot: 0=none, 1=TeamColor, 2=TeamGlow (TYPE_INT).
    PB_LONGITUDE    = 46,   ///< Internal — derived from LineEmitter flag (TYPE_FLOAT, invisible).
};

constexpr int PARAM_COUNT_MAX   = 500;  ///< Hard cap on live particle count.
constexpr int NUM_PARAMS        = 47;   ///< Total number of parameters in the PB2 descriptor.
constexpr int TICKS_PER_SEC     = 4800; ///< 3ds Max timeline ticks per second (fixed at 4800).

/**
 * @brief Texture category entry for the replaceable-texture combo box.
 */
struct TexCategoryEntry {
    std::wstring name;  ///< Display label shown in the combo box.
    int          index = 0; ///< Replaceable texture index written to the PB2.
};

/// @cond FORWARD_DECLS
class GenParticle;
class Wc3Particles2Particle;
class Wc3Particles2ClassDesc;
class Wc3Particles2ParticleDraw;
class EmitterCreateCallback;
class Wc3Particles2DlgProc;
/// @endcond

/**
 * @brief Base particle system object, extending SimpleParticle with PB2 support.
 *
 * Manages parameter block lifetime, particle birth/update/death simulation,
 * emitter mesh construction, viewport display, hit-testing, and serialisation.
 * Concrete plugin identity and per-particle overrides are provided by the
 * derived @ref Wc3Particles2Particle class.
 */
class GenParticle : public SimpleParticle {
public:
    MSTR                    m_particlePath;     ///< Texture filename.
    MSTR                    m_texturePrefix;    ///< Texture path prefix (e.g. "Textures\\").
    int                     stepSize = 0;       ///< Integration step size in timeline ticks.
    std::vector<Point3>     birthPos;           ///< World position at birth, per slot (used for tail quads).
    IParamBlock2*           pblock2 = nullptr;  ///< Owned ParamBlock2 reference (ref index 0).
    std::mt19937            m_rng;              ///< Mersenne Twister PRNG for deterministic particle birth.

    explicit GenParticle();
    ~GenParticle() override;

    /// @name Particle core
    /// @{
    /// @brief Initialises one particle slot at the given birth time and position.
    void    BirthParticle(INode* node, TimeValue bt, int index, TimeValue dt);
    /// @brief Resets the particle array to @p count dead slots, ready for simulation.
    void    ComputeParticleStart(TimeValue t0, INode* node);
    /// @brief Returns the number of currently alive particles.
    int     CountLive();
    /// @brief Returns the clamped maximum particle count from the PB2.
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

    /// @name Cross-DLL interface
    /// @{
    void* GetInterface(ULONG id) override;
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
    /// @brief Computes the local-space bounding box from PB2 width/height.
    void    GetLocalBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box) override;
    void    GetWorldBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box) override;
    /// @}

    /// @name Viewport display
    /// @{
    /// @brief Draws the emitter wireframe and all live particles in the viewport.
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
 * @brief Concrete 3ds Max plugin class registered under WC3PARTICLES2_CLASS_ID.
 *
 * Provides the Class_ID, object name, and per-particle size/centre/render
 * overrides. All simulation logic is inherited from @ref GenParticle.
 */
class Wc3Particles2Particle : public GenParticle {
public:
    Wc3Particles2Particle();
    ~Wc3Particles2Particle() override;

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
 * Draws each particle as a camera-facing billboard quad (head), an
 * axis-aligned streak quad from birth position to current position (tail),
 * or both. Colour and scale are interpolated over the particle lifetime
 * using start/mid/end keyframes.
 */
class Wc3Particles2ParticleDraw : public CustomParticleDisplay {
public:
    GenParticle*    obj      = nullptr; ///< Owning particle object (provides birthPos).
    int             life     = 0;       ///< Particle lifetime in ticks.
    int             partType = 0;       ///< Render mode: 0=Head, 1=Tail, 2=Both.
    float           tailLen  = 0.0f;    ///< Tail length multiplier.
    float           midtime  = 0.5f;    ///< Normalised time for the colour/scale mid-keyframe.
    float           scaleStart = 10.0f; ///< Scale at birth.
    float           scaleMid   = 10.0f; ///< Scale at mid-life.
    float           scaleEnd   = 10.0f; ///< Scale at death.
    Point3          colorStart = Point3(1,1,1); ///< Colour at birth.
    Point3          colorMid   = Point3(1,1,1); ///< Colour at mid-life.
    Point3          colorEnd   = Point3(1,1,1); ///< Colour at death.
    BOOL            xyQuad     = FALSE;  ///< Align quads to XY plane instead of facing the camera.
    BOOL            modelSpace = FALSE;  ///< Particles are in emitter-local space.
    Matrix3         emitterTM;           ///< Emitter object transform (world space).
    Matrix3         invEmitterTM;        ///< Cached inverse of @ref emitterTM.

    Wc3Particles2ParticleDraw() = default;
    BOOL DrawParticle(GraphicsWindow* gw, ParticleSys& parts, int i) override;
};

/**
 * @brief Plugin ClassDesc2 singleton for Wc3Particles2.
 *
 * Returned by @ref GetWc3Particles2Desc() and registered with 3ds Max via
 * @c LibClassDesc(). Controls visibility, creation, and identity of the plugin.
 */
class Wc3Particles2ClassDesc : public ClassDesc2 {
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
 * @brief Viewport mouse-creation callback for the emitter rectangle.
 *
 * Handles the two-click creation gesture: first click sets one corner,
 * second click sets the opposite corner to define the emitter plane.
 */
class EmitterCreateCallback : public CreateMouseCallBack {
public:
    GenParticle*    rain = nullptr; ///< Particle object being created.
    IPoint2         sp0;             ///< First click: screen position.
    Point3          p0{0, 0, 0};    ///< First click: world-space position.
    IPoint2         sp1;             ///< Second click: screen position.
    Point3          p1{0, 0, 0};    ///< Second click: world-space position.

    EmitterCreateCallback() = default;
    int proc(ViewExp* vpt, int msg, int point, int flags,
             IPoint2 m, Matrix3& mat) override;
};

/**
 * @brief ParamMap2 dialog callback for custom rollup controls.
 *
 * Handles initialisation of the file-path CustEdit, the replaceable-texture
 * combo box, the auto-computed count spinner, and the max-rate label.
 * Also responds to spinner changes on PB_LIFE and PB_INITVEL to keep
 * the displayed max-rate value in sync.
 */
class Wc3Particles2DlgProc : public ParamMap2UserDlgProc {
public:
    GenParticle* po; ///< Owning particle object.

    /// @param p  Owning GenParticle whose UI is being driven.
    explicit Wc3Particles2DlgProc(GenParticle* p) : po(p) {}
    ~Wc3Particles2DlgProc() override = default;

    INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                    UINT msg, WPARAM wParam, LPARAM lParam) override;
    void    DeleteThis() override { delete this; }

    /// @brief Recomputes the auto-count and max-rate label from PB_LIFE and PB_INITVEL.
    void    SetupMaxRate(HWND hWnd, IParamBlock2* pb, TimeValue t);
    /// @brief Opens a file-open dialog and writes the result to the path CustEdit.
    void    BrowseForMdlFile(HWND hWnd);
};

/**
 * @brief DlgProc for the Import/Export configuration rollout.
 */
class ConfigDlgProc : public ParamMap2UserDlgProc {
public:
    GenParticle* po;
    BOOL loadDynamic;  ///< Whether to import animated keyframes.
    explicit ConfigDlgProc(GenParticle* p) : po(p), loadDynamic(TRUE) {}
    ~ConfigDlgProc() override = default;
    INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                    UINT msg, WPARAM wParam, LPARAM lParam) override;
    void    DeleteThis() override { delete this; }
    void    ExportConfig(HWND hWnd, IParamBlock2* pb, TimeValue t);
    void    ImportConfig(HWND hWnd, IParamBlock2* pb, TimeValue t);
};

/// @name Free utility functions
/// @{

/// @brief Loads a string resource by ID into an internal static buffer.
/// @return Pointer to the loaded string, or nullptr on failure.
const MCHAR*    GetString(UINT id);

/// @brief Returns the global ClassDesc2 singleton for Wc3Particles2.
ClassDesc2*     GetWc3Particles2Desc();

/// @brief Deep-copies an active ParticleSys (points, ages, radii, tensions).
void            ParticleCacheData(ParticleSys* dst, ParticleSys* src);

/// @brief Returns TRUE if the matrix has negative (mirrored) parity.
BOOL            Parity(Matrix3* tm);

/// @brief Flips the winding order of every face in @p mesh.
void            FlipAllMeshFaces(Mesh* mesh);

/// @brief Populates @p list with the available replaceable-texture categories.
void            BuildTexCategoryList(std::vector<TexCategoryEntry>& list);

/// @}

// ============================================================================
// Globals
// ============================================================================
extern HINSTANCE    hInstance;
