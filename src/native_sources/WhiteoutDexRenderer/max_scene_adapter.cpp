// ============================================================================
// WhiteoutDex Max Scene Adapter — Implements IModelSource for 3ds Max scenes
// Refactored from extract.cpp. All Max SDK coupling lives here.
// ============================================================================

#include "max_scene_adapter.h"
#include "renderer/model/model_source_utils.h"
#include "whiteout/flakes/content_provider.h"
#include "whiteout/flakes/types.h"
#include "whiteout/flakes/util/coordinate_system.h"
#include "whiteout/flakes/util/team_glow_data.h"
#include "whiteout/flakes/util/texture_image_usage.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <numbers>
#include <sstream>

// clang-format off
#include <Windows.h>
#include <maxscript/foundation/numbers.h>
#include <maxscript/maxscript.h>
// clang-format on

using namespace whiteout::flakes;
using namespace whiteout::flakes::io;
using namespace whiteout::flakes::renderer;
using namespace whiteout::flakes::renderer::model;
using namespace whiteout::flakes::renderer::effects;

static Point3 GetVNormal(Mesh& mesh, i32 faceIdx, i32 vertIdx);

// ----------------------------------------------------------------------------
// File-local helpers:
//   * Coord-space bridge — 3ds Max is always Max-space; these lift Max
//     positions/directions into the renderer-native default. No-op when
//     WDX_DEFAULT_COORD_SPACE=Max.
//   * Traversal / material / texture primitives shared by every phase.
// ----------------------------------------------------------------------------
namespace {

inline Vector3f MaxPointToDefault(const Point3& p) {
    return CoordinateSystem::ToDefault(CoordSpace::Max, Vector3f{p.x, p.y, p.z});
}
inline Vector3f MaxDirToDefault(const Point3& n) {
    return CoordinateSystem::ToDefaultDir(CoordSpace::Max, Vector3f{n.x, n.y, n.z});
}

inline bool CaseInsensitiveEqual(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (usize i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<u8>(a[i])) != std::tolower(static_cast<u8>(b[i])))
            return false;
    }
    return true;
}

inline bool CaseInsensitiveContains(std::string_view hay, std::string_view needle) {
    if (needle.empty() || needle.size() > hay.size())
        return needle.empty();
    for (usize i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (usize j = 0; j < needle.size(); ++j) {
            if (std::tolower(static_cast<u8>(hay[i + j])) !=
                std::tolower(static_cast<u8>(needle[j]))) {
                ok = false;
                break;
            }
        }
        if (ok)
            return true;
    }
    return false;
}

// Mirror of corn_effects_emitter::ParseAnimVisibilityGuide +
// SetCurrentAnimationName: decide whether a Popcorn emitter is "on" for a
// given current-sequence name. We replicate the logic in the adapter
// because the renderer's evaluation reads `mi.animation.ActiveSequenceIndex()`,
// and for the Max plugin the adapter's GetSequences() returns empty (Max
// owns the timeline). Without this gate every popcorn emitter renders on
// every sequence regardless of its guide.
inline bool EvaluateGuideForSequence(const std::string& guide,
                                     const std::string& currentName) {
    if (guide.empty())
        return true;

    bool defaultEnabled = true;
    std::vector<std::string> enabledNames;
    std::vector<std::string> disabledNames;
    bool sawAlways = false;
    bool sawEnable = false;

    auto trim = [](std::string& s) {
        usize a = 0;
        while (a < s.size() && std::isspace(static_cast<u8>(s[a])))
            ++a;
        usize b = s.size();
        while (b > a && std::isspace(static_cast<u8>(s[b - 1])))
            --b;
        s = s.substr(a, b - a);
    };

    std::stringstream ss(guide);
    std::string token;
    while (std::getline(ss, token, ',')) {
        trim(token);
        if (token.empty())
            continue;
        std::string name = token;
        bool enabled = true;
        const auto eq = token.find('=');
        if (eq != std::string::npos) {
            name = token.substr(0, eq);
            std::string val = token.substr(eq + 1);
            trim(name);
            trim(val);
            enabled = CaseInsensitiveEqual(val, "on");
        }
        if (CaseInsensitiveEqual(name, "always")) {
            defaultEnabled = enabled;
            sawAlways = true;
        } else if (enabled) {
            enabledNames.push_back(std::move(name));
            sawEnable = true;
        } else {
            disabledNames.push_back(std::move(name));
        }
    }

    // No explicit `always` but at least one enable token → engine treats
    // listed names as the only ones enabled (implicit default-OFF).
    if (!sawAlways && sawEnable)
        defaultEnabled = false;

    // Empty current-name (e.g. no sequences pushed) leaves the default —
    // never flips into a name-based override.
    if (currentName.empty())
        return defaultEnabled;

    bool result = defaultEnabled;
    const auto& search = defaultEnabled ? disabledNames : enabledNames;
    for (const auto& s : search) {
        if (CaseInsensitiveContains(currentName, s)) {
            result = !defaultEnabled;
            break;
        }
    }
    return result;
}

// Depth-first traversal of the scene's root children, invoking `fn(node)` on
// every descendant. Handles the "no interface available" case by returning
// early (callers treat this as an empty collection).
template <typename Fn>
void ForEachSceneNode(Fn fn) {
    Interface* ip = GetCOREInterface();
    if (!ip)
        return;
    std::function<void(INode*)> recurse = [&](INode* node) {
        if (!node)
            return;
        fn(node);
        for (i32 c = 0; c < node->NumberOfChildren(); c++)
            recurse(node->GetChildNode(c));
    };
    INode* root = ip->GetRootNode();
    for (i32 i = 0; i < root->NumberOfChildren(); i++)
        recurse(root->GetChildNode(i));
}

// Iterate Wc3Material layers: if `mtl` IS a Wc3Material, call `fn(mtl, 0)`;
// otherwise iterate sub-materials and invoke `fn(sub, layerIdx)` for each
// Wc3Material sub-material (non-Wc3 sub-materials are skipped, matching
// CollectMaterials' Wc3-only layer list).
template <typename Fn>
void ForEachWc3SubMtl(Mtl* mtl, Fn fn) {
    if (!mtl)
        return;
    if (mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
        fn(mtl, 0);
        return;
    }
    if (mtl->NumSubMtls() <= 0)
        return;
    i32 layerIdx = 0;
    for (i32 si = 0; si < mtl->NumSubMtls(); si++) {
        Mtl* sub = mtl->GetSubMtl(si);
        if (sub && sub->ClassID() == WARCRAFT3_MAT_CLASS_ID)
            fn(sub, layerIdx++);
    }
}

// Unwrap a Texmap* to its underlying BitmapTex. Returns tex itself when it's
// a native BitmapTex; for the legacy Wc3Bitmap wrapper, returns the first
// contained BitmapTex sub-reference; otherwise nullptr. Used by every path
// that needs to peek at the bitmap filename or its UVGen.
inline BitmapTex* UnwrapBitmapTex(Texmap* tex) {
    if (!tex)
        return nullptr;
    if (tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0))
        return static_cast<BitmapTex*>(tex);
    if (tex->ClassID() == WC3_BITMAP_CLASS_ID) {
        for (i32 r = 0; r < tex->NumRefs(); r++) {
            ReferenceTarget* ref = tex->GetReference(r);
            if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0))
                return static_cast<BitmapTex*>(ref);
        }
    }
    return nullptr;
}

// True when the path names a 3ds Max .ifl (Image File List). The MDLXImporter
// stores KMTF texture flipbooks as a BitmapTex pointing at a generated .ifl,
// which the texture decoder can't read as an image — the frames inside it
// have to be loaded individually.
inline bool IsIflPath(const std::wstring& path) {
    if (path.size() < 4)
        return false;
    std::wstring ext = path.substr(path.size() - 4);
    for (auto& c : ext)
        c = (wchar_t)towlower(c);
    return ext == L".ifl";
}

// Decode an IFL line from UTF-8. Max reads .ifl entries as UTF-8 (verified
// against Max 2027: a UTF-8 entry loads, an ANSI-code-page one does not) and
// MDLXImporter writes them that way. Widening the bytes one-by-one into
// wchar_t instead — which is what this used to do — turned '月' (E6 9C 88)
// into three bogus characters, so any frame under a non-ANSI directory
// resolved to a path that does not exist and the texture came up missing.
// For an all-ASCII line the result is unchanged.
inline std::wstring IflUtf8ToWide(const std::string& s) {
    if (s.empty())
        return {};
    const int n = static_cast<int>(s.size());
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), n, nullptr, 0);
    if (len <= 0)
        return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), n, out.data(), len);
    return out;
}

// Read the frame paths out of a .ifl file — one path per line (the importer
// writes absolute paths). Trims whitespace, skips blank lines; returns empty
// when the file can't be opened.
std::vector<std::wstring> ReadIflFrames(const std::wstring& iflPath) {
    std::vector<std::wstring> frames;
    std::ifstream ifs(iflPath.c_str());
    if (!ifs.is_open())
        return frames;
    std::string line;
    bool firstLine = true;
    while (std::getline(ifs, line)) {
        // A hand-authored .ifl may carry a UTF-8 BOM; Max accepts one, so skip
        // it rather than folding it into the first frame's path.
        if (firstLine) {
            firstLine = false;
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
                line.erase(0, 3);
        }
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                                 line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        if (start > 0)
            line.erase(0, start);
        frames.push_back(IflUtf8ToWide(line));
    }
    return frames;
}

// Max 2022+ exposes GetObjectName(bool); earlier versions need GetClassName(MSTR&)
// with the MSTR holder outliving the returned pointer. Callers pass their own
// MSTR so the storage stays in scope.
inline const MCHAR* GetObjectClassName(Object* obj, [[maybe_unused]] MSTR& scratch) {
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
    return obj->GetObjectName(false);
#else
    obj->GetClassName(scratch);
    return scratch.data();
#endif
}

// DFS over an Animatable's param blocks, invoking `fn(pblock, pid, paramDef)`
// for the first param whose int_name matches `name` (case-insensitive). Returns
// true if the param was found. The five typed PB2 getters share this scan.
template <typename Fn>
bool FindPB2Param(Animatable* anim, const wchar_t* name, Fn fn) {
    if (!anim)
        return false;
    for (i32 pb = 0; pb < anim->NumParamBlocks(); pb++) {
        IParamBlock2* pblock = anim->GetParamBlock(pb);
        if (!pblock)
            continue;
        for (i32 p = 0; p < pblock->NumParams(); p++) {
            ParamID pid = pblock->IndextoID(p);
            ParamDef& def = pblock->GetParamDef(pid);
            if (def.int_name && _wcsicmp(def.int_name, name) == 0) {
                fn(pblock, pid, def);
                return true;
            }
        }
    }
    return false;
}

} // namespace

// ============================================================================
// Ctor / Dtor
// ============================================================================

MaxSceneAdapter::MaxSceneAdapter() {}
MaxSceneAdapter::~MaxSceneAdapter() {}

// ============================================================================
// PackMatrix: Matrix3 (Max-space row-major) → Matrix44f (renderer-native)
// 3ds Max gives us a row-major Max-space transform; pack it, then conjugate
// into the renderer-native space so every downstream caller (bones,
// attachments, emitters, cameras) receives default-space matrices.
// ============================================================================

Matrix44f MaxSceneAdapter::PackMatrix(const Matrix3& tm) {
    Matrix44f m = Matrix44f::identity();
    for (i32 r = 0; r < 3; r++) {
        Point3 row = tm.GetRow(r);
        m.data[r][0] = row.x;
        m.data[r][1] = row.y;
        m.data[r][2] = row.z;
        m.data[r][3] = 0.0f;
    }
    Point3 trans = tm.GetRow(3);
    m.data[3][0] = trans.x;
    m.data[3][1] = trans.y;
    m.data[3][2] = trans.z;
    m.data[3][3] = 1.0f;
    return CoordinateSystem::ToDefault(CoordSpace::Max, m);
}

// ============================================================================
// PB2*Or — typed PB2 reads that collapse the `int v = def; if (PB2Int(...)) ...`
// idiom into a single call. Return `def` on miss, the read value otherwise.
// ============================================================================

i32 MaxSceneAdapter::PB2IntOr(Animatable* a, const wchar_t* name, TimeValue t, i32 def) {
    i32 v = def;
    PB2Int(a, name, t, v);
    return v;
}
f32 MaxSceneAdapter::PB2FloatOr(Animatable* a, const wchar_t* name, TimeValue t, f32 def) {
    f32 v = def;
    PB2Float(a, name, t, v);
    return v;
}
bool MaxSceneAdapter::PB2BoolOr(Animatable* a, const wchar_t* name, TimeValue t, bool def) {
    BOOL v = def ? TRUE : FALSE;
    PB2Bool(a, name, t, v);
    return v != 0;
}

// ============================================================================
// IParamBlock2 helpers — the five typed getters share FindPB2Param's scan and
// only differ in how they unpack the matched ParamDef + pblock value.
// ============================================================================

bool MaxSceneAdapter::PB2Float(Animatable* anim, const wchar_t* name, TimeValue t, f32& out) {
    return FindPB2Param(anim, name, [&](IParamBlock2* pblock, ParamID pid, ParamDef& def) {
        Interval iv = FOREVER;
        if (def.type == TYPE_INT) {
            i32 v = 0;
            pblock->GetValue(pid, t, v, iv);
            out = (f32)v;
        } else {
            pblock->GetValue(pid, t, out, iv);
        }
    });
}

bool MaxSceneAdapter::PB2Int(Animatable* anim, const wchar_t* name, TimeValue t, i32& out) {
    return FindPB2Param(anim, name, [&](IParamBlock2* pblock, ParamID pid, ParamDef& def) {
        Interval iv = FOREVER;
        if (def.type == TYPE_FLOAT) {
            f32 v = 0;
            pblock->GetValue(pid, t, v, iv);
            out = (i32)v;
        } else {
            pblock->GetValue(pid, t, out, iv);
        }
    });
}

bool MaxSceneAdapter::PB2Bool(Animatable* anim, const wchar_t* name, TimeValue t, BOOL& out) {
    return FindPB2Param(anim, name, [&](IParamBlock2* pblock, ParamID pid, ParamDef&) {
        Interval iv = FOREVER;
        i32 val = 0;
        pblock->GetValue(pid, t, val, iv);
        out = val ? TRUE : FALSE;
    });
}

bool MaxSceneAdapter::PB2Color(Animatable* anim, const wchar_t* name, TimeValue t, Color& out) {
    return FindPB2Param(anim, name, [&](IParamBlock2* pblock, ParamID pid, ParamDef&) {
        // If any channel > 1 the value is in [0-255] (TYPE_RGBA / scripted
        // #color); otherwise it's already [0-1] (TYPE_POINT3 / colorswatch).
        Color cv = pblock->GetColor(pid, t);
        if (cv.r > 1.0f || cv.g > 1.0f || cv.b > 1.0f)
            out = Color(cv.r / 255.0f, cv.g / 255.0f, cv.b / 255.0f);
        else
            out = cv;
    });
}

bool MaxSceneAdapter::PB2Texmap(Animatable* anim, const wchar_t* name, Texmap*& out) {
    return FindPB2Param(anim, name, [&](IParamBlock2* pblock, ParamID pid, ParamDef&) {
        Interval iv = FOREVER;
        pblock->GetValue(pid, 0, out, iv);
    });
}

u32 MaxSceneAdapter::ReadWrapFlagsFromTexmap(Texmap* tex) {
    if (!tex)
        return 0x3;

    // Wc3Bitmap: the WrapWidth / WrapHeight authored values live as PB2
    // booleans named `wrapU` / `wrapV` (see Wc3Bitmap.ms). Prefer these over
    // the delegate's UVGen bits because the scripted plugin treats them as
    // the source of truth.
    if (tex->ClassID() == WC3_BITMAP_CLASS_ID) {
        u32 flags = 0;
        if (PB2BoolOr(tex, L"wrapU", 0, false))
            flags |= 0x1;
        if (PB2BoolOr(tex, L"wrapV", 0, false))
            flags |= 0x2;
        return flags;
    }

    // Plain BitmapTex (and the Wc3Bitmap delegate, in case we miss the PB2
    // params for some legacy node): pull U_WRAP / V_WRAP off the StdUVGen.
    if (BitmapTex* bmt = UnwrapBitmapTex(tex)) {
        if (StdUVGen* uv = bmt->GetUVGen()) {
            const int tiling = uv->GetTextureTiling();
            u32 flags = 0;
            if (tiling & U_WRAP)
                flags |= 0x1;
            if (tiling & V_WRAP)
                flags |= 0x2;
            return flags;
        }
    }

    return 0x3;
}

Object* MaxSceneAdapter::GetBaseObject(INode* node) {
    if (!node)
        return nullptr;
    Object* obj = node->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
        obj = static_cast<IDerivedObject*>(obj)->GetObjRef();
    return obj;
}

Modifier* MaxSceneAdapter::FindSkinModifier(INode* node) {
    return FindModifierByClassID(node, SKIN_CLASSID);
}

Modifier* MaxSceneAdapter::FindModifierByClassID(INode* node, Class_ID cid) {
    if (!node)
        return nullptr;
    Object* objRef = node->GetObjectRef();
    while (objRef && objRef->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* dobj = static_cast<IDerivedObject*>(objRef);
        for (i32 i = 0; i < dobj->NumModifiers(); i++) {
            Modifier* mod = dobj->GetModifier(i);
            if (mod && mod->ClassID() == cid)
                return mod;
        }
        objRef = dobj->GetObjRef();
    }
    return nullptr;
}

Modifier* MaxSceneAdapter::FindModifierByClassName(INode* node,
                                                   const wchar_t* const* nameSubstrings) {
    if (!node || !nameSubstrings)
        return nullptr;
    Object* obj = node->GetObjectRef();
    i32 safety = 32; // bound the IDerivedObject chain so a self-referential
                     // stack can't hang the walker (mirrors the exporter).
    while (obj && safety-- > 0) {
        if (obj->SuperClassID() != GEN_DERIVOB_CLASS_ID)
            break;
        IDerivedObject* dobj = static_cast<IDerivedObject*>(obj);
        const i32 n = dobj->NumModifiers();
        if (n < 0 || n > 256)
            break;
        for (i32 i = 0; i < n; i++) {
            Modifier* mod = dobj->GetModifier(i);
            if (!mod)
                continue;
            MSTR cname;
            mod->GetClassName(cname);
            const wchar_t* nm = cname.data();
            if (!nm)
                continue;
            for (i32 k = 0; nameSubstrings[k]; k++) {
                if (wcsstr(nm, nameSubstrings[k]) != nullptr)
                    return mod;
            }
        }
        Object* next = dobj->GetObjRef();
        if (next == obj)
            break; // self-reference guard
        obj = next;
    }
    return nullptr;
}

std::wstring MaxSceneAdapter::GetMaxFilePath() {
    Interface* ip = GetCOREInterface();
    if (!ip)
        return L"";
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
    const MCHAR* fp = ip->GetCurFilePath().data();
#else
    const MCHAR* fp = ip->GetCurFilePath();
#endif
    if (!fp || !fp[0])
        return L"";
    std::wstring path(fp);
    usize pos = path.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? path.substr(0, pos + 1) : L"";
}

// ============================================================================
// Texture loading (stores pixel data for GetTextures() instead of renderer calls)
// ============================================================================

// True when every character of the path is plain ASCII, i.e. it round-trips
// through the narrow shared-asset key that NormalizeTextureKey produces by
// truncating each wchar_t to a byte. Anything outside that range turns into a
// different byte (U+6708 '月' -> 0x08), so the key names no real file.
static bool PathIsNarrowSafe(const std::wstring& p) {
    for (wchar_t c : p)
        if (c < 0x20 || c > 0x7E)
            return false;
    return true;
}

// Read raw bytes from a wide-string disk path; ext receives the lowercased extension.
static bool ReadFileBytesFromDisk(const std::wstring& filePath, std::vector<u8>& out,
                                  std::string& ext) {
    std::filesystem::path p(filePath);
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    if (out.empty())
        return false;
    ext = ExtensionLower(p);
    return true;
}

// ============================================================================
// RegisterTexture — single entry point for stashing a decoded RGBA8 buffer
// into both loadedTextures_ and texEntries_. All loader paths funnel here so
// there's only one definition of "what an entry looks like".
// ============================================================================

i32 MaxSceneAdapter::RegisterTexture(const std::wstring& key, i32 replaceableId,
                                     std::vector<u8>&& pixels, i32 width, i32 height,
                                     const std::wstring& displayPath, std::string sharedKey,
                                     u32 wrapFlags) {
    i32 id = nextTexId_++;
    if (!key.empty())
        texPathToId_[key] = id;
    loadedTextures_.push_back({id, replaceableId, std::move(pixels), width, height,
                               std::move(sharedKey), wrapFlags});
    TextureEntry te;
    te.textureId = id;
    te.replaceableId = replaceableId;
    te.filePath = displayPath.empty() ? key : displayPath;
    texEntries_.push_back(te);
    return id;
}

// EnsureHdTeamColorSentinel removed — adapters now set
// MaterialLayerData::teamColorMapId = kHdTeamColorActive when the HD layer
// flags its team-colour slot as live-driven. ReplaceableTextureManager owns
// the live swatch texture; no per-model sentinel allocation is needed.

// ============================================================================
// ResolveBitmapPath — resolve a BitmapTex filename from a Mtl's named texmap
// slot. Accepts either a native BitmapTex directly or the legacy Wc3Bitmap
// wrapper (which keeps a real BitmapTex as its first matching sub-reference).
// Returns the empty string when the slot is empty, isn't a BitmapTex, or has
// no filename assigned.
// ============================================================================

std::wstring MaxSceneAdapter::ResolveBitmapPath(Mtl* mtl, const wchar_t* paramName) {
    Texmap* tex = nullptr;
    if (!PB2Texmap(mtl, paramName, tex))
        return {};
    BitmapTex* bmt = UnwrapBitmapTex(tex);
    if (!bmt)
        return {};
    const MCHAR* fn = bmt->GetMapName();
    return (fn && fn[0]) ? std::wstring(fn) : std::wstring{};
}

// ============================================================================
// ReadWc3MaterialFlags — pack the six Wc3Material bool toggles into the
// renderer's compact MaterialLayerData::flags bitmask.
// ============================================================================

i32 MaxSceneAdapter::ReadWc3MaterialFlags(Mtl* mtl) {
    static constexpr struct {
        const wchar_t* name;
        i32 bit;
    } kFlags[] = {
        {L"twoSided", 1},    {L"unshaded", 2},    {L"unfogged", 4},
        {L"noDepthTest", 8}, {L"noDepthSet", 16}, {L"constantColor", 32},
    };
    i32 flags = 0;
    for (auto& f : kFlags) {
        if (PB2BoolOr(mtl, f.name, 0, false))
            flags |= f.bit;
    }
    return flags;
}

// ============================================================================
// SnapshotMaterial / UpdateMaterialSnapshots — drive the material-change
// detection used by RefreshMaterials. Both CollectScene and RefreshMaterials
// previously duplicated this property read; now they share one implementation.
// ============================================================================

MaxSceneAdapter::MaterialSnapshot MaxSceneAdapter::SnapshotMaterial(Mtl* mtl) {
    MaterialSnapshot snap;
    snap.filterMode = MapFilterMode(PB2IntOr(mtl, L"filterMode", 0, 1) - 1);
    snap.flags = ReadWc3MaterialFlags(mtl);
    snap.replaceableTexture = std::max(0, PB2IntOr(mtl, L"replaceableId", 0, 1) - 1);
    // Shader dropdown is 1-based (1=SD, 2=HD, 3=SDOnHD, 4=Crystal → renderer 24).
    i32 shaderType = PB2IntOr(mtl, L"shaderType", 0, 1);
    snap.shaderId = (shaderType == 4) ? 24 : std::max(0, shaderType - 1);
    snap.sortOrder = std::max(0, PB2IntOr(mtl, L"sortOrder", 0, 1) - 1);
    snap.priorityPlane = PB2IntOr(mtl, L"priorityPlane", 0, 0);
    snap.texturePath = ResolveBitmapPath(mtl, L"diffuseMap");
    snap.normalTexPath = ResolveBitmapPath(mtl, L"normalMap");
    snap.ormTexPath = ResolveBitmapPath(mtl, L"ormMap");
    snap.emissiveTexPath = ResolveBitmapPath(mtl, L"emissiveMap");
    snap.teamColorTexPath = ResolveBitmapPath(mtl, L"teamColorMap");
    {
        // Newly authored (or removed) UV animation must trigger a material
        // re-upload — the layer's textureAnimationId lives in the surface
        // table, not in the per-frame state.
        Texmap* diffTex = nullptr;
        PB2Texmap(mtl, L"diffuseMap", diffTex);
        snap.hasUvAnim = FindUvAnimSource(mtl, diffTex).any();
    }
    return snap;
}

void MaxSceneAdapter::UpdateMaterialSnapshots() {
    matSnapshots_.clear();
    for (auto& mi : materials_) {
        ForEachWc3SubMtl(mi.mtl, [&](Mtl* sub, i32 layerIdx) {
            // Single Wc3Material → key by materialId; composite → combined key.
            const i32 key = (sub == mi.mtl) ? mi.materialId : (mi.materialId * 1000 + layerIdx);
            matSnapshots_[key] = SnapshotMaterial(sub);
        });
    }
}

// Load a texture from the FileContentProvider (CASC/MPQ fallback).
// archivePath should be a WC3-relative path like "Textures\\Dirt.blp"
// (forward or back slashes are both accepted by the provider).
// Returns a texture id on success, or -1 if not found.
i32 MaxSceneAdapter::LoadTextureFromContentProvider(const std::string& archivePath,
                                                    i32 replaceableId, u32 wrapFlags) {
    if (archivePath.empty())
        return -1;

    // Use the wide-string key so we share the texPathToId_ cache.
    std::wstring wkey(archivePath.begin(), archivePath.end());
    auto cached = texPathToId_.find(wkey);
    if (cached != texPathToId_.end())
        return cached->second;

    // Cross-model dedup: skip the CASC extraction + decode entirely when
    // the renderer has the archive path cached from a previous model load.
    std::string sharedKey = NormalizeTextureKey(archivePath);
    if (IsTextureCached(sharedKey)) {
        mprintf(_M("    [ContentProvider] cache hit, skipping decode: '%S'\n"),
                archivePath.c_str());
        return RegisterTexture(wkey, replaceableId, {}, 0, 0, /*displayPath*/ L"",
                               std::move(sharedKey), wrapFlags);
    }

    std::string foundExt;
    auto data = contentProvider_.ReadFile(archivePath, &foundExt);
    if (!data || data->empty()) {
        mprintf(_M("    [ContentProvider] not found: '%S'\n"), archivePath.c_str());
        return -1;
    }
    if (foundExt.empty())
        foundExt = ExtensionLower(std::filesystem::path(archivePath));

    std::vector<u8> pixels;
    i32 w = 0, h = 0;
    if (!DecodeToRGBA8(*data, foundExt, pixels, w, h)) {
        mprintf(_M("    [ContentProvider] parse failed: '%S'\n"), archivePath.c_str());
        return -1;
    }
    i32 id = RegisterTexture(wkey, replaceableId, std::move(pixels), w, h,
                             /*displayPath*/ L"", std::move(sharedKey), wrapFlags);
    mprintf(_M("    [ContentProvider] loaded '%S' as texId=%d (%dx%d)\n"), archivePath.c_str(), id,
            w, h);
    return id;
}

// Decode an entire Max Bitmap into a tight RGBA8 buffer (and optionally sum
// per-channel totals for diagnostics). The caller owns the returned pixels.
static std::vector<u8> BitmapToRGBA8(Bitmap* bmp, i32 w, i32 h, long long* sumR = nullptr,
                                     long long* sumG = nullptr, long long* sumB = nullptr,
                                     long long* sumA = nullptr) {
    std::vector<u8> rgba(usize(w) * usize(h) * 4);
    std::vector<BMM_Color_64> line(w);
    long long r = 0, g = 0, b = 0, a = 0;
    for (i32 y = 0; y < h; y++) {
        bmp->GetPixels(0, y, w, line.data());
        for (i32 x = 0; x < w; x++) {
            i32 idx = (y * w + x) * 4;
            rgba[idx] = (u8)(line[x].r >> 8);
            rgba[idx + 1] = (u8)(line[x].g >> 8);
            rgba[idx + 2] = (u8)(line[x].b >> 8);
            rgba[idx + 3] = (u8)(line[x].a >> 8);
            r += rgba[idx];
            g += rgba[idx + 1];
            b += rgba[idx + 2];
            a += rgba[idx + 3];
        }
    }
    if (sumR)
        *sumR = r;
    if (sumG)
        *sumG = g;
    if (sumB)
        *sumB = b;
    if (sumA)
        *sumA = a;
    return rgba;
}

// Load a file through the Max bitmap manager and return its contents decoded
// to RGBA8. Returns nullopt if the load fails or throws — callers fall back
// to direct-decode and content-provider paths on failure. Optional sum*
// out-params receive per-channel totals for diagnostic logging.
struct MaxBitmapRGBA {
    std::vector<u8> rgba;
    i32 width = 0, height = 0;
};
static std::optional<MaxBitmapRGBA> LoadMaxBitmapRGBA(
    const std::wstring& filePath, const wchar_t* logPrefix, long long* sumR = nullptr,
    long long* sumG = nullptr, long long* sumB = nullptr, long long* sumA = nullptr) {
    Bitmap* bmp = nullptr;
    BMMRES status = BMMRES_IOERROR;
    try {
        BitmapInfo bi;
        bi.SetName(filePath.c_str());
        bmp = TheManager->Load(&bi, &status);
    } catch (...) {
        mprintf(_M("  %s [exception in TheManager->Load] '%s'\n"), logPrefix, filePath.c_str());
    }
    if (!bmp || status != BMMRES_SUCCESS) {
        mprintf(_M("  %s [Max bitmap failed, status=%d] '%s'\n"), logPrefix, (i32)status,
                filePath.c_str());
        return std::nullopt;
    }
    MaxBitmapRGBA out;
    out.width = bmp->Width();
    out.height = bmp->Height();
    out.rgba = BitmapToRGBA8(bmp, out.width, out.height, sumR, sumG, sumB, sumA);
    bmp->DeleteThis();
    return out;
}

i32 MaxSceneAdapter::LoadTexture(const std::wstring& filePath, i32 replaceableId,
                                 u32 wrapFlags, bool skipMaxBitmapManager) {
    if (replaceableId != 0) {
        // Replaceable slot: adapter only declares the id. The renderer-
        // side ReplaceableTextureManager::RegisterModelSlot path bakes
        // pixels (TeamColor/TeamGlow ids 1/2) or loads canonical CASC
        // assets (higher ids) once the actor is staged. We dedupe by
        // a synthetic key so two emitters declaring the same id share
        // one slot.
        wchar_t buf[32];
        swprintf_s(buf, L"__REPL_%d__", replaceableId);
        std::wstring key = buf;
        auto it = texPathToId_.find(key);
        if (it != texPathToId_.end())
            return it->second;
        return RegisterTexture(key, replaceableId, {}, 0, 0, L"", {}, wrapFlags);
    }

    if (filePath.empty())
        return -1;
    auto it = texPathToId_.find(filePath);
    if (it != texPathToId_.end())
        return it->second;

    // Cross-model dedup: if the renderer's shared cache already has this
    // path, reserve a borrow slot WITHOUT decoding. UploadStagedTextures
    // sees the empty rgba + non-empty sharedKey and binds via
    // TextureAssetManager::BindShared. This skips the entire bitmap-
    // manager / disk / CASC pipeline for textures another model loaded.
    // Only claim a shared key when the path survives the trip into a narrow
    // string. The key is NOT just a cache tag: ModelLoader::uploadStagedGpu
    // binds an AssetManager slot from it via ContentRef::FromPath and then
    // `continue`s — our decoded pixels are never uploaded, and the slot
    // renders placeholder WHITE until the host's FileContentProvider reads
    // that path back off disk. NormalizeTextureKey builds the key by
    // truncating each wchar_t to a byte, so a model under a non-ANSI
    // directory (a CJK folder, say) yields a key naming no real file — the
    // texture then stays white forever even though we decoded it fine.
    // Leaving the key empty for those paths sends them down the plain
    // Upload path with the pixels we already have. Only cross-model dedup is
    // given up, and only for paths that could never have deduped correctly.
    std::string sharedKey = PathIsNarrowSafe(filePath) ? NormalizeTextureKey(filePath)
                                                       : std::string{};
    if (!sharedKey.empty() && IsTextureCached(sharedKey)) {
        mprintf(_M("  Texture: [cache hit, skipping decode] '%s'\n"), filePath.c_str());
        return RegisterTexture(filePath, 0, {}, 0, 0, /*displayPath*/ L"", std::move(sharedKey),
                               wrapFlags);
    }

    // Primary path: delegate to 3ds Max's bitmap manager — unless the caller
    // asked to skip it (normal maps). Max's bitmap manager loses the raw
    // R/G channels on BC5 / DXT5n normal maps and hands us a flat-grey
    // RGBA8, which the HD shader then samples as a wrong tangent-space
    // normal. Direct-decode preserves the encoded channels.
    if (!skipMaxBitmapManager) {
        long long sumR = 0, sumG = 0, sumB = 0, sumA = 0;
        if (auto bmp = LoadMaxBitmapRGBA(filePath, L"Texture:", &sumR, &sumG, &sumB, &sumA)) {
            const i32 total = bmp->width * bmp->height;
            i32 id = RegisterTexture(filePath, 0, std::move(bmp->rgba), bmp->width, bmp->height,
                                     /*displayPath*/ L"", sharedKey, wrapFlags);
            mprintf(_M("  Texture %d: %dx%d avgRGBA=[%d,%d,%d,%d] '%s'\n"), id, bmp->width,
                    bmp->height, (i32)(sumR / total), (i32)(sumG / total), (i32)(sumB / total),
                    (i32)(sumA / total), filePath.c_str());
            return id;
        }
        mprintf(_M("  Texture: trying direct decode for '%s'\n"), filePath.c_str());
    } else {
        mprintf(_M("  Texture: [normal map — direct decode only] '%s'\n"), filePath.c_str());
    }

    // Fallback 1: direct decode with our own parsers.
    {
        std::vector<u8> fileBytes;
        std::string ext;
        std::vector<u8> pixels;
        i32 pw = 0, ph = 0;
        if (ReadFileBytesFromDisk(filePath, fileBytes, ext) &&
            DecodeToRGBA8(fileBytes, ext, pixels, pw, ph)) {
            i32 id = RegisterTexture(filePath, replaceableId, std::move(pixels), pw, ph,
                                     /*displayPath*/ L"", sharedKey, wrapFlags);
            mprintf(_M("  Texture %d: %dx%d [direct decode] '%s'\n"), id, pw, ph, filePath.c_str());
            return id;
        }
    }

    // Fallback 2: CASC/MPQ by filename.
    {
        std::string narrowName = std::filesystem::path(filePath).filename().string();
        mprintf(_M("  Texture: [ContentProvider fallback] '%S'\n"), narrowName.c_str());
        i32 id = LoadTextureFromContentProvider(narrowName, replaceableId, wrapFlags);
        if (id >= 0) {
            texPathToId_[filePath] = id; // alias the wide path to the archive id
            return id;
        }
    }

    // All fallbacks exhausted — magenta placeholder. No sharedKey: a missing
    // file should NOT poison the dedup cache for everyone else.
    std::vector<u8> rgba;
    FillSolidRGBA(rgba, 4, 4, 255, 0, 255, 255);
    i32 id = RegisterTexture(filePath, 0, std::move(rgba), 4, 4, L"", {}, wrapFlags);
    mprintf(_M("  Texture %d: [missing] %s\n"), id, filePath.c_str());
    return id;
}

// LoadTextureWithTeamColor removed — WC3's SD engine ignores the
// authored diffuse for TEAMCOLOR layers and binds a flat swatch at t0.
// The TEAMCOLOR branch now funnels through LoadTexture(L"", 1) so the
// slot is populated by ReplaceableTextureManager::BakeSlot from the
// current swatch (and re-baked live on SetTeamColor, matching HD).

// GenerateTeamGlowTexture removed — the TEAMGLOW branch in
// ExtractWc3MaterialLayer now calls LoadTexture(L"", 2), which reserves a
// textureId with replaceableId=2 and leaves the pixel bake to
// ReplaceableTextureManager on first RegisterModelSlot.

// ============================================================================
// CollectScene — called once on main thread before Get*()
// ============================================================================

void MaxSceneAdapter::CollectScene() {
    nextTexId_ = 0;
    nextMatId_ = 0;
    texPathToId_.clear();
    mtlToId_.clear();
    loadedTextures_.clear();
    texEntries_.clear();
    bones_.clear();
    boneNodeToIdx_.clear();
    geosets_.clear();
    materials_.clear();
    particles_.clear();
    pe1Emitters_.clear();
    popcornEmitters_.clear();
    attachments_.clear();
    ribbons_.clear();
    collisions_.clear();
    lights_.clear();

    CollectGeometry();
    CollectMaterials();
    CollectBones();
    CollectAttachments();
    CollectParticleEmitters();
    CollectRibbonEmitters();
    CollectCollisionShapes();
    CollectLights();

    // Capture the initial material-state snapshot so RefreshMaterials can
    // detect per-property changes on subsequent frames.
    UpdateMaterialSnapshots();
}

// ============================================================================
// Collect geometry (identical logic to old extract.cpp)
// ============================================================================

void MaxSceneAdapter::CollectGeometry() {
    geosets_.clear();
    i32 geosetId = 0;

    ForEachSceneNode([&](INode* node) {
        if (node->IsNodeHidden())
            return;
        Object* baseObj = GetBaseObject(node);
        if (!baseObj)
            return;
        if (baseObj->SuperClassID() != GEOMOBJECT_CLASS_ID)
            return;
        // PE2 / ribbon helpers are geometry at the SDK level but handled
        // separately by the emitter collectors.
        if (baseObj->ClassID() == WC3PARTICLES2_CLASS_ID ||
            baseObj->ClassID() == WC3RIBBON_CLASS_ID)
            return;
        if (!baseObj->CanConvertToType(triObjectClassID))
            return;

        MSTR classNameBuf;
        const MCHAR* className = GetObjectClassName(baseObj, classNameBuf);
        if (_wcsicmp(className, L"Editable Mesh") != 0 &&
            _wcsicmp(className, L"Editable Poly") != 0)
            return;

        // Skip geometry whose only material slot is a high-index replaceable
        // with no actual bitmap bound — these are placeholder meshes.
        Mtl* mtl = node->GetMtl();
        if (mtl && mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
            Texmap* diffTex = nullptr;
            PB2Texmap(mtl, L"diffuseMap", diffTex);
            i32 retex = 0;
            if (diffTex && diffTex->ClassID() == WC3_BITMAP_CLASS_ID) {
                retex = std::max(0, PB2IntOr(diffTex, L"replaceableId", 0, 1) - 1);
            }
            if (retex >= 4 && !diffTex) {
                mprintf(_M("  [Skipped replaceable %d geoset '%s']\n"), retex, node->GetName());
                return;
            }
        }

        GeosetInfo gi;
        gi.geosetId = geosetId++;
        gi.node = node;
        geosets_.push_back(gi);
    });
}

// ============================================================================
// Extract a single Wc3Material into a MaterialLayerInfo
// ============================================================================

MaterialLayerInfo MaxSceneAdapter::ExtractWc3MaterialLayer(Mtl* mtl) {
    MaterialLayerInfo layer;

    layer.filterMode = MapFilterMode(PB2IntOr(mtl, L"filterMode", 0, 1) - 1);
    layer.alpha = std::min(PB2FloatOr(mtl, L"opacity", 0, 100.0f) / 100.0f, 1.0f);
    layer.replaceableTexture = std::max(0, PB2IntOr(mtl, L"replaceableId", 0, 1) - 1);
    // Shader dropdown is 1-based (1=SD, 2=HD, 3=SDOnHD, 4=Crystal → renderer 24).
    i32 shaderType = PB2IntOr(mtl, L"shaderType", 0, 1);
    layer.shaderId = (shaderType == 4) ? 24 : std::max(0, shaderType - 1);
    layer.flags = ReadWc3MaterialFlags(mtl);

    // Reforged PBR knobs. The fresnelColor trio is only written when at least
    // one component was authored — zero defaults mean "classic MDX, no fresnel".
    layer.emissiveGain = PB2FloatOr(mtl, L"emissiveGain", 0, layer.emissiveGain);
    layer.fresnelOpacity = PB2FloatOr(mtl, L"fresnelOpacity", 0, layer.fresnelOpacity);
    layer.fresnelTeamColor = PB2FloatOr(mtl, L"fresnelTeamCol", 0, layer.fresnelTeamColor);
    {
        f32 fr = 0, fg = 0, fb = 0;
        const bool hasR = PB2Float(mtl, L"fresnelR", 0, fr);
        const bool hasG = PB2Float(mtl, L"fresnelG", 0, fg);
        const bool hasB = PB2Float(mtl, L"fresnelB", 0, fb);
        if (hasR || hasG || hasB)
            layer.fresnelColor = {fr, fg, fb};
    }

    // HD subtexture slots. Missing paths stay at -1; the renderer treats
    // negative ids as "slot absent". Wrap flags are lifted from the source
    // texmap (Wc3Bitmap's wrapU/wrapV or plain BitmapTex's StdUVGen).
    auto loadSlot = [&](const wchar_t* paramName, bool skipMaxBitmap = false) -> i32 {
        Texmap* slotTex = nullptr;
        if (!PB2Texmap(mtl, paramName, slotTex) || !slotTex)
            return -1;
        BitmapTex* bmt = UnwrapBitmapTex(slotTex);
        const MCHAR* fn = bmt ? bmt->GetMapName() : nullptr;
        if (!fn || !fn[0])
            return -1;
        return LoadTexture(std::wstring(fn), 0, ReadWrapFlagsFromTexmap(slotTex), skipMaxBitmap);
    };
    // Normal maps go straight to direct-decode: Max's bitmap manager
    // collapses BC5 / DXT5n normals into a flat-grey RGBA8 and the HD
    // shader ends up sampling a constant normal across the surface.
    layer.normalMapId = loadSlot(L"normalMap", /*skipMaxBitmap=*/true);
    layer.ormMapId = loadSlot(L"ormMap");
    layer.emissiveMapId = loadSlot(L"emissiveMap");

    // teamColorMap is special — the slot accepts both:
    //   (a) a Wc3 replaceable=1 placeholder (Wc3Bitmap with replaceableId=1,
    //       usually no path) → renderer fills with the live UI swatch at
    //       draw time. We map this to kHdTeamColorActive so the HD draw
    //       knows to bind the per-actor HD swatch at t4.
    //   (b) any other Wc3Bitmap (custom mask BLP / DDS the artist dropped
    //       in this slot) → carry the real loaded texture id through and
    //       let the HD draw bind it like a regular material slot.
    //
    // We read the assigned Texmap's replaceableId directly so a custom
    // mask plus replaceableId=1 still picks the swatch (engine convention
    // is "replaceableId is authoritative for this slot's binding").
    {
        Texmap* tcTex = nullptr;
        const bool hasTexmap = PB2Texmap(mtl, L"teamColorMap", tcTex) && tcTex;
        const bool isWc3Bitmap = hasTexmap && tcTex->ClassID() == WC3_BITMAP_CLASS_ID;
        const i32 tcReplId =
            isWc3Bitmap ? std::max(0, PB2IntOr(tcTex, L"replaceableId", 0, 1) - 1) : 0;

        // The MDLXImporter writes plain BitmapTex (not Wc3Bitmap) for every
        // texture entry, so a Reforged HD model arrives here with the canonical
        // ReplaceableTextures\TeamColor\TeamColor00.blp baked into the slot.
        // Without this detection the static red swatch loads as a normal
        // texture and ignores Actor::teamColor on retint.
        const std::wstring tcPath = hasTexmap ? ResolveBitmapPath(mtl, L"teamColorMap")
                                              : std::wstring{};
        auto isCanonicalTeamColorPath = [](const std::wstring& w) {
            if (w.empty()) return false;
            std::wstring lower = w;
            for (auto& c : lower) {
                if (c >= L'A' && c <= L'Z') c = wchar_t(c + (L'a' - L'A'));
                if (c == L'/') c = L'\\';
            }
            return lower.find(L"replaceabletextures\\teamcolor\\teamcolor") !=
                   std::wstring::npos;
        };
        const bool isCanonicalTC = isCanonicalTeamColorPath(tcPath);

        if (isWc3Bitmap && tcReplId == 1) {
            // Live swatch placeholder.
            layer.teamColorMapId = kHdTeamColorActive;
        } else if (isCanonicalTC && (layer.shaderId == 1 || layer.shaderId == 24)) {
            // Plain BitmapTex pointing at the canonical TeamColor swatch on an
            // HD/Crystal layer — treat as live swatch so SetTeamColor retints.
            layer.teamColorMapId = kHdTeamColorActive;
        } else {
            // Try to load whatever path the user assigned; -1 if none.
            layer.teamColorMapId = loadSlot(L"teamColorMap");
            // Fallback: HD layer with a Texmap at this slot but no path
            // (e.g. an empty BitmapTex). Treat as the swatch placeholder
            // so the slot still drives team-colour blending at draw time.
            if (layer.teamColorMapId < 0 && hasTexmap &&
                (layer.shaderId == 1 || layer.shaderId == 24)) {
                layer.teamColorMapId = kHdTeamColorActive;
            }
        }
    }

    Texmap* diffuseTexmap = nullptr;
    PB2Texmap(mtl, L"diffuseMap", diffuseTexmap);
    const std::wstring baseTexPath = ResolveBitmapPath(mtl, L"diffuseMap");
    i32 baseTexId = -1;
    if (!baseTexPath.empty() && IsIflPath(baseTexPath)) {
        // Animated texture (KMTF flipbook): the BitmapTex points at a .ifl
        // list, not an image. Load every frame as its own texture; the first
        // frame becomes the layer's static texture and Evaluate() swaps the
        // frames per-tick via FrameState::layerTextureIds using the timing
        // the importer stored on the BitmapTex (startTime / playbackRate /
        // endCondition).
        const u32 wrapFlags = ReadWrapFlagsFromTexmap(diffuseTexmap);
        const std::vector<std::wstring> frames = ReadIflFrames(baseTexPath);
        IflAnim anim;
        for (const auto& f : frames) {
            const i32 id = LoadTexture(f, 0, wrapFlags);
            if (id >= 0)
                anim.frameTexIds.push_back(id);
        }
        mprintf(_M("    \x2192 diffuse IFL '%s': %d/%d frames loaded\n"), baseTexPath.c_str(),
                (i32)anim.frameTexIds.size(), (i32)frames.size());
        if (!anim.frameTexIds.empty()) {
            baseTexId = anim.frameTexIds[0];
            BitmapTex* bmt = UnwrapBitmapTex(diffuseTexmap);
            if (anim.frameTexIds.size() > 1 && bmt) {
                const TimeValue tpf = GetTicksPerFrame();
                const f32 rate = bmt->GetPlaybackRate();
                anim.startTime = bmt->GetStartTime();
                anim.intervalTicks =
                    (rate > 0.0001f) ? (TimeValue)((f32)tpf / rate + 0.5f) : tpf;
                if (anim.intervalTicks <= 0)
                    anim.intervalTicks = tpf;
                anim.endCondition = bmt->GetEndCondition();
                iflAnims_[mtl] = std::move(anim);
            }
        }
    } else if (!baseTexPath.empty()) {
        mprintf(_M("    \x2192 diffuse path: '%s'\n"), baseTexPath.c_str());
        baseTexId = LoadTexture(baseTexPath, 0, ReadWrapFlagsFromTexmap(diffuseTexmap));
    } else {
        // Diagnostic: distinguish "no texmap" from "texmap but not a BitmapTex".
        if (diffuseTexmap)
            mprintf(
                _M("    \x2192 diffuseMap texmap is NOT a BitmapTexture (e.g. Mix/Composite)\n"));
        else
            mprintf(_M("    \x2192 NO texmap found for 'diffuseMap' property\n"));
    }

    // TeamColor / TeamGlow / replaceable-id resolution.
    if (layer.replaceableTexture == 1 && !baseTexPath.empty()) {
        if (layer.shaderId == 1 || layer.shaderId == 24) {
            // HD: team color comes from the live UI swatch at t4 (teamColorMapId
            // acts as the enable flag). Keep diffuse as-is; baking a static
            // composite here would be wrong.
            mprintf(_M("    \x2192 HD TEAMCOLOR branch: diffuse='%s', shaderId=%d\n"),
                    baseTexPath.c_str(), layer.shaderId);
            layer.textureId = baseTexId;
            if (layer.teamColorMapId < 0)
                layer.teamColorMapId = kHdTeamColorActive;
        } else {
            // SD TEAMCOLOR: WC3's engine ignores whatever BLP the MDX/Max
            // material points at for this slot and binds a flat team-colour
            // swatch at t0 — the SD shader samples the swatch verbatim, no
            // alpha-mask composite. Reserve a replaceableId=1 slot here and
            // ReplaceableTextureManager::BakeSlot fills the pixels from the
            // current swatch (and rebakes on SetTeamColor for live retint).
            mprintf(
                _M("    \x2192 TEAMCOLOR branch: live swatch slot (source BLP ignored: '%s')\n"),
                baseTexPath.c_str());
            layer.textureId = LoadTexture(L"", 1);
            layer.filterMode = 0;
            layer.alpha = 1.0f;
        }
    } else if (layer.replaceableTexture == 2) {
        mprintf(_M("    \x2192 TEAMGLOW branch: registering live slot\n"));
        // Renderer-side ReplaceableTextureManager owns the TGA bake now —
        // the adapter just reserves a textureId with replaceableId=2.
        layer.textureId = LoadTexture(L"", 2);
    } else if (layer.replaceableTexture >= 1 && baseTexId >= 0) {
        mprintf(_M("    \x2192 REPLACEABLE branch: replTex=%d, baseTexId=%d\n"),
                layer.replaceableTexture, baseTexId);
        layer.textureId = baseTexId;
        layer.filterMode = 0;
        layer.alpha = 1.0f;
    } else if (layer.replaceableTexture >= 1 && baseTexId < 0) {
        mprintf(_M("    \x2192 SOLID REPLACEABLE branch: replTex=%d\n"), layer.replaceableTexture);
        layer.textureId = LoadTexture(L"", layer.replaceableTexture);
    } else {
        layer.textureId = baseTexId;
    }

    // TXAN: register this layer's UV-animation source (Wc3Bitmap anim_*
    // controllers, StdUVGen tracks, or legacy material-level params) so
    // Evaluate() can feed the renderer's texAnimPalette.
    layer.textureAnimationId = RegisterUvAnimSource(mtl, diffuseTexmap);

    return layer;
}

// ============================================================================
// UV animation (TXAN) source resolution
// ============================================================================

// A channel only counts as animated when a controller is assigned: the
// exporter builds TXAN exclusively from controllers, so a static non-default
// offset doesn't move in-game and must not move in the preview either.
MaxSceneAdapter::UvAnimSource MaxSceneAdapter::FindUvAnimSource(Mtl* mtl, Texmap* diffuseTex) {
    // PB2 params only grow a controller once animation is authored, so
    // presence alone is the signal (unlike PB1, see the StdUVGen path).
    auto pb2Animated = [](Animatable* a) {
        static const wchar_t* kChannels[] = {L"anim_UOffset", L"anim_VOffset", L"anim_WAngle",
                                             L"anim_UTiling", L"anim_VTiling"};
        for (const wchar_t* ch : kChannels) {
            Control* c = nullptr;
            FindPB2Param(a, ch, [&](IParamBlock2* pb, ParamID pid, ParamDef&) {
                const int animIdx = pb->GetAnimNum(pid, 0);
                if (animIdx >= 0 && animIdx < pb->NumSubs())
                    if (Animatable* sub = pb->SubAnim(animIdx))
                        c = GetControlInterface(sub);
            });
            if (c)
                return true;
        }
        return false;
    };

    UvAnimSource src;
    if (diffuseTex && diffuseTex->ClassID() == WC3_BITMAP_CLASS_ID) {
        if (pb2Animated(diffuseTex))
            src.texmap = diffuseTex;
    } else if (BitmapTex* bmt = UnwrapBitmapTex(diffuseTex)) {
        // Plain BitmapTex (importer fallback when the scripted plugin is
        // missing): the TXAN controllers live on the StdUVGen. Its PB1
        // params ALWAYS carry a controller, so gate on IsAnimated().
        // Stable sub-anim indices: 0=U_Offset 1=V_Offset 2=U_Tiling
        // 3=V_Tiling 6=W_Angle (matches wc3_material_extractor.cpp).
        if (StdUVGen* uvg = bmt->GetUVGen()) {
            static const int kSubs[] = {0, 1, 2, 3, 6};
            for (int si : kSubs) {
                if (si >= uvg->NumSubs())
                    continue;
                Animatable* sub = uvg->SubAnim(si);
                Control* c = sub ? GetControlInterface(sub) : nullptr;
                if (c && c->IsAnimated()) {
                    src.texmap = diffuseTex;
                    break;
                }
            }
        }
    }
    // Legacy: scenes saved with the pre-2.3.0 material plugin that still
    // carried the anim_* params on the Wc3Material itself.
    if (!src.texmap && mtl && pb2Animated(mtl))
        src.legacyMtl = mtl;
    return src;
}

i32 MaxSceneAdapter::RegisterUvAnimSource(Mtl* mtl, Texmap* diffuseTex) {
    UvAnimSource src = FindUvAnimSource(mtl, diffuseTex);
    if (!src.any())
        return -1;
    void* key = src.texmap ? (void*)src.texmap : (void*)src.legacyMtl;
    if (auto it = uvAnimSrcToId_.find(key); it != uvAnimSrcToId_.end())
        return it->second;
    const i32 id = (i32)uvAnimSources_.size();
    uvAnimSources_.push_back(src);
    uvAnimSrcToId_[key] = id;
    return id;
}

// ============================================================================
// Collect materials — supports single Wc3Material and Composite materials
// ============================================================================

void MaxSceneAdapter::CollectMaterials() {
    materials_.clear();
    mtlToId_.clear();
    iflAnims_.clear();
    uvAnimSources_.clear();
    uvAnimSrcToId_.clear();
    nextMatId_ = 0;

    // Copy sortOrder/priorityPlane from a Wc3Material onto MaterialInfo.
    auto fillMaterialSort = [&](MaterialInfo& mi, Mtl* src) {
        mi.sortOrder = std::max(0, PB2IntOr(src, L"sortOrder", 0, 1) - 1);
        mi.priorityPlane = PB2IntOr(src, L"priorityPlane", 0, 0);
    };
    // Generic fallback: treat the material as a single-layer Std mat, binding
    // its first SubTexmap if it's a BitmapTex. Shared by the "composite with no
    // Wc3 sub-materials" and "plain non-Wc3 material" code paths.
    auto pushGenericLayer = [&](MaterialInfo& mi, Mtl* src) {
        MaterialLayerInfo layer;
        layer.flags = 1;
        if (src->NumSubTexmaps() > 0) {
            Texmap* sub = src->GetSubTexmap(0);
            if (BitmapTex* bmt = UnwrapBitmapTex(sub)) {
                const MCHAR* fname = bmt->GetMapName();
                if (fname && fname[0]) {
                    std::wstring path(fname);
                    if (IsIflPath(path)) {
                        // .ifl flipbook on a non-Wc3 material: at least show
                        // the first frame instead of a failed white load.
                        const auto frames = ReadIflFrames(path);
                        if (!frames.empty())
                            layer.textureId =
                                LoadTexture(frames[0], 0, ReadWrapFlagsFromTexmap(sub));
                    } else {
                        layer.textureId = LoadTexture(path, 0, ReadWrapFlagsFromTexmap(sub));
                    }
                }
            }
        }
        mi.layers.push_back(layer);
    };

    for (auto& gs : geosets_) {
        Mtl* mtl = gs.node->GetMtl();
        if (!mtl)
            continue;
        if (auto it = mtlToId_.find(mtl); it != mtlToId_.end()) {
            gs.materialId = it->second;
            continue;
        }

        MaterialInfo mi;
        mi.materialId = nextMatId_++;
        mi.mtl = mtl;
        gs.materialId = mi.materialId;
        mtlToId_[mtl] = mi.materialId;

        if (mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
            // Single Wc3Material — one layer, material-level sort comes from
            // the same Wc3Material.
            mprintf(_M("  Mat %d '%s': Wc3Material\n"), mi.materialId, gs.node->GetName());
            fillMaterialSort(mi, mtl);
            mi.layers.push_back(ExtractWc3MaterialLayer(mtl));
        } else if (mtl->NumSubMtls() > 0) {
            // Composite / multi-material: take sort + priority from the first
            // Wc3 sub-material; subsequent Wc3 layers contribute per-layer blend.
            // Non-Wc3 sub-materials are skipped (matches the snapshot iteration).
            const i32 numSubs = mtl->NumSubMtls();
            mprintf(_M("  Mat %d '%s': Composite (%d sub-materials)\n"), mi.materialId,
                    gs.node->GetName(), numSubs);
            for (i32 si = 0; si < numSubs; si++) {
                Mtl* subMtl = mtl->GetSubMtl(si);
                if (!subMtl)
                    continue;
                if (subMtl->ClassID() != WARCRAFT3_MAT_CLASS_ID) {
                    mprintf(_M("    Layer %d: non-Wc3 material '%s' (skipped)\n"), si,
                            subMtl->GetName().data());
                    continue;
                }
                mprintf(_M("    Layer %d: Wc3Material '%s'\n"), si, subMtl->GetName().data());
                if (mi.layers.empty())
                    fillMaterialSort(mi, subMtl);
                mi.layers.push_back(ExtractWc3MaterialLayer(subMtl));
            }
            // No Wc3Material sub-materials found — treat the composite itself
            // as a generic one-layer Std material.
            if (mi.layers.empty())
                pushGenericLayer(mi, mtl);
        } else {
            // Generic non-Wc3 material — single-layer Std fallback.
            pushGenericLayer(mi, mtl);
        }
        materials_.push_back(mi);
    }
}

// ============================================================================
// Collect bones
// ============================================================================

void MaxSceneAdapter::CollectBones() {
    bones_.clear();
    boneNodeToIdx_.clear();

    std::vector<INode*> allBones;
    std::unordered_map<INode*, bool> boneSet;
    ForEachSceneNode([&](INode* node) {
        Modifier* skinMod = FindSkinModifier(node);
        if (!skinMod)
            return;
        ISkin* skin = (ISkin*)skinMod->GetInterface(I_SKIN);
        if (!skin)
            return;
        for (i32 b = 0; b < skin->GetNumBones(); b++) {
            INode* bn = skin->GetBone(b);
            if (bn && !boneSet[bn]) {
                boneSet[bn] = true;
                allBones.push_back(bn);
            }
        }
    });

    for (i32 i = 0; i < (i32)allBones.size(); i++) {
        BoneInfo bi;
        bi.node = allBones[i];
        bi.index = i;
        bones_.push_back(bi);
        boneNodeToIdx_[allBones[i]] = i;
    }

    // Unskinned meshes: the mesh node itself acts as a bone, mirroring the
    // exporter (which generates a bone per unskinned mesh). GetSkinWeights
    // rigid-binds the geoset to this entry so node-level mesh animation —
    // its own keys, a custom pivot, links to animated parents — plays in
    // the preview. A node can already be here as another mesh's skin bone;
    // reuse that index.
    for (auto& gs : geosets_) {
        if (!gs.node || FindSkinModifier(gs.node))
            continue;
        if (boneNodeToIdx_.count(gs.node))
            continue;
        BoneInfo bi;
        bi.node = gs.node;
        bi.index = (i32)bones_.size();
        boneNodeToIdx_[gs.node] = bi.index;
        bones_.push_back(bi);
    }
}

// ============================================================================
// Collect attachments (Wc3_AttachPoint helpers)
// ============================================================================

void MaxSceneAdapter::CollectAttachments() {
    attachments_.clear();
    i32 idx = 0;

    ForEachSceneNode([&](INode* node) {
        Object* baseObj = GetBaseObject(node);
        if (!baseObj || baseObj->ClassID() != WC3ATTACHPOINT_CLASS_ID)
            return;

        AttachmentInfo ai;
        ai.index = idx++;
        ai.node = node;
        ai.attachmentId = PB2IntOr(baseObj, L"attachmentId", 0, 0);

        if (PB2BoolOr(baseObj, L"usesExternalModel", 0, false)) {
            // TYPE_STRING isn't covered by the typed PB2Or helpers; pull the
            // value directly through the FindPB2Param scaffold.
            FindPB2Param(static_cast<Animatable*>(baseObj), L"externalModelPath",
                         [&](IParamBlock2* pblock, ParamID pid, ParamDef&) {
                             const MCHAR* sv = nullptr;
                             Interval iv = FOREVER;
                             pblock->GetValue(pid, 0, sv, iv);
                             if (sv && sv[0]) {
                                 std::wstring wp(sv);
                                 ai.modelPath = std::string(wp.begin(), wp.end());
                             }
                         });
        }
        if (!ai.modelPath.empty()) {
            mprintf(_M("  [Attachment '%s'] id=%d model='%S'\n"), node->GetName(), ai.attachmentId,
                    ai.modelPath.c_str());
        }
        attachments_.push_back(ai);
    });
}

// ============================================================================
// Collect particle emitters
// ============================================================================

void MaxSceneAdapter::CollectParticleEmitters() {
    particles_.clear();
    pe1Emitters_.clear();
    popcornEmitters_.clear();
    i32 emitterId = 0;
    i32 pe1EmitterId = 0;
    i32 popcornEmitterId = 0;
    const std::wstring basePath = GetMaxFilePath();

    // True when the path exists on disk.
    auto existsOnDisk = [](const std::wstring& p) {
        return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    };

    ForEachSceneNode([&](INode* node) {
        if (node->IsNodeHidden())
            return;
        Object* baseObj = GetBaseObject(node);
        if (!baseObj)
            return;

        if (baseObj->ClassID() == WC3PARTICLES2_CLASS_ID) {
            ParticleEmitterInfo pi;
            pi.emitterId = emitterId++;
            pi.node = node;

            // Texture path/prefix come from cross-DLL GetInterface calls.
            std::wstring texPath, texFile;
            if (auto* p = static_cast<const MSTR*>(baseObj->GetInterface(WC3P2_TEXTURE_PATH_IID));
                p && p->Length() > 0)
                texFile = p->data();
            if (auto* p = static_cast<const MSTR*>(baseObj->GetInterface(WC3P2_TEXTURE_PREFIX_IID));
                p && p->Length() > 0)
                texPath = p->data();
            const i32 replId = PB2IntOr(baseObj, L"ReplaceableId", 0, 0);

            if (!texFile.empty()) {
                mprintf(_M("  [Particle '%s'] prefix='%s' file='%s'\n"), node->GetName(),
                        texPath.c_str(), texFile.c_str());

                // Try disk paths in order: basePath+prefix+file, basePath\Textures\file, bare
                // filename.
                std::wstring fp = basePath + texPath + texFile;
                mprintf(_M("    Try1: '%s' %s\n"), fp.c_str(),
                        existsOnDisk(fp) ? _M("FOUND") : _M("not found"));
                if (!existsOnDisk(fp)) {
                    fp = basePath + L"Textures\\" + texFile;
                    mprintf(_M("    Try2: '%s' %s\n"), fp.c_str(),
                            existsOnDisk(fp) ? _M("FOUND") : _M("not found"));
                }
                if (!existsOnDisk(fp)) {
                    fp = texFile;
                    mprintf(_M("    Try3 (filename only): '%s'\n"), fp.c_str());
                }

                if (existsOnDisk(fp)) {
                    pi.textureId = LoadTexture(fp, replId);
                } else {
                    // Fall back to CASC/MPQ. Normalise separators for archive keys.
                    std::string narrowPrefix(texPath.begin(), texPath.end());
                    std::string narrowFile(texFile.begin(), texFile.end());
                    for (auto& c : narrowPrefix)
                        if (c == '/')
                            c = '\\';
                    for (auto& c : narrowFile)
                        if (c == '/')
                            c = '\\';
                    std::string archivePath = narrowPrefix + narrowFile;
                    mprintf(_M("    Try4 (ContentProvider): '%S'\n"), archivePath.c_str());
                    pi.textureId = LoadTextureFromContentProvider(archivePath, replId);
                    if (pi.textureId < 0) {
                        mprintf(_M("    Try5 (ContentProvider, no prefix): '%S'\n"),
                                narrowFile.c_str());
                        pi.textureId = LoadTextureFromContentProvider(narrowFile, replId);
                    }
                    if (pi.textureId < 0)
                        pi.textureId = LoadTexture(fp, replId); // magenta placeholder
                }
                pi.replaceableId = replId;
                mprintf(_M("    \x2192 texId=%d\n"), pi.textureId);
            } else if (replId > 0) {
                // No file set, but replaceable (TeamColor / TeamGlow).
                pi.textureId = LoadTexture(L"", replId);
                pi.replaceableId = replId;
            } else {
                mprintf(_M("  [Particle '%s'] NO texture file set!\n"), node->GetName());
            }
            particles_.push_back(pi);
            return;
        }

        if (baseObj->ClassID() == WC3PARTICLES1_CLASS_ID) {
            PE1EmitterInfo pi;
            pi.emitterId = pe1EmitterId++;
            pi.node = node;
            if (auto* p = static_cast<const MSTR*>(baseObj->GetInterface(WC3P1_MODEL_PATH_IID));
                p && p->Length() > 0) {
                std::wstring wp = p->data();
                pi.modelPath = std::string(wp.begin(), wp.end());
            }
            mprintf(_M("  [PE1 '%s'] model='%S'\n"), node->GetName(), pi.modelPath.c_str());
            if (!pi.modelPath.empty())
                pe1Emitters_.push_back(pi);
            return;
        }

        // BlizzPopcorn detection: ClassID comparison is unreliable for
        // simpleManipulator scripted plugins (Max wraps the declared id
        // opaquely — same reason WC3ATTACHPOINT_CLASS_ID above points at
        // a NeoDex GUID). Match on the SuperClassID + class name, mirroring
        // how CollectCollisionShapes picks up Wc3CollisionSphere / Box.
        bool isPopcorn = false;
        if (baseObj->SuperClassID() == HELPER_CLASS_ID) {
            MSTR cnBuf;
            const MCHAR* cn = GetObjectClassName(baseObj, cnBuf);
            if (cn && (wcsstr(cn, L"Wc3 Popcorn") || wcsstr(cn, L"Wdx_Wc3Popcorn") ||
                       wcsstr(cn, L"BlizzPopcorn")))
                isPopcorn = true;
        }
        if (isPopcorn) {
            // BlizzPopcorn (Reforged corn emitter). The popcornPath string is
            // either the resolved local-disk .pkb / .pkfx the importer
            // extracted, or — when extraction failed — the canonical relative
            // path like "Effects\Foo.pkb"; the renderer's content provider
            // handles both. animVisibilityGuide comes from rawFlags (engine's
            // raw passthrough); empty means "always on".
            PopcornEmitterInfo pi;
            pi.emitterId = popcornEmitterId++;
            pi.node = node;
            // ReplaceableId / team color are no longer authored on the
            // scripted plugin — engine doesn't use them for Popcorn. Keep
            // the field defaulted to 0 in PopcornEmitterInfo.
            pi.cornEffectsScaling = PB2BoolOr(baseObj, L"flagScaling", 0, false);

            // TYPE_STRING isn't covered by the typed PB2 helpers — read both
            // string params through the FindPB2Param scaffold.
            auto readStr = [&](const wchar_t* name, std::string& out) {
                FindPB2Param(static_cast<Animatable*>(baseObj), name,
                             [&](IParamBlock2* pblock, ParamID pid, ParamDef&) {
                                 const MCHAR* sv = nullptr;
                                 Interval iv = FOREVER;
                                 pblock->GetValue(pid, 0, sv, iv);
                                 if (sv && sv[0]) {
                                     std::wstring wp(sv);
                                     out.assign(wp.begin(), wp.end());
                                 }
                             });
            };
            readStr(L"popcornPath", pi.pkbPath);
            readStr(L"rawFlags", pi.animVisibilityGuide);

            mprintf(_M("  [Popcorn '%s'] pkb='%S' replId=%d guide='%S'\n"),
                    node->GetName(), pi.pkbPath.c_str(), pi.replaceableId,
                    pi.animVisibilityGuide.c_str());
            popcornEmitters_.push_back(pi);
            return;
        }
    });
}

// ============================================================================
// Collect ribbon emitters
// ============================================================================

void MaxSceneAdapter::CollectRibbonEmitters() {
    ribbons_.clear();
    i32 emitterId = 0;

    ForEachSceneNode([&](INode* node) {
        if (node->IsNodeHidden())
            return;
        Object* baseObj = GetBaseObject(node);
        if (!baseObj || baseObj->ClassID() != WC3RIBBON_CLASS_ID)
            return;

        RibbonEmitterInfo ri;
        ri.emitterId = emitterId++;
        ri.node = node;

        // Wc3Ribbon stores its material in PB2 param "Material" (pb_material=7).
        // Fall back to the node material if the emitter didn't wire one.
        Mtl* mtl = nullptr;
        for (i32 pb = 0; pb < baseObj->NumParamBlocks(); pb++) {
            IParamBlock2* pblock = static_cast<Animatable*>(baseObj)->GetParamBlock(pb);
            if (!pblock)
                continue;
            mtl = pblock->GetMtl(7, 0);
            if (mtl)
                break;
        }
        if (!mtl)
            mtl = node->GetMtl();

        if (mtl) {
            std::wstring path;
            Texmap* srcTex = nullptr;
            if (mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
                PB2Texmap(mtl, L"diffuseMap", srcTex);
                path = ResolveBitmapPath(mtl, L"diffuseMap");
            } else {
                srcTex = mtl->GetSubTexmap(ID_DI);
                if (BitmapTex* bmt = UnwrapBitmapTex(srcTex)) {
                    const MCHAR* fname = bmt->GetMapName();
                    if (fname && fname[0])
                        path = fname;
                }
            }
            if (!path.empty())
                ri.textureId = LoadTexture(path, 0, ReadWrapFlagsFromTexmap(srcTex));
        }
        ribbons_.push_back(ri);
    });
}

// ============================================================================
// Collect collision shapes
// ============================================================================

void MaxSceneAdapter::CollectCollisionShapes() {
    collisions_.clear();
    ForEachSceneNode([&](INode* node) {
        if (node->IsNodeHidden())
            return;
        Object* baseObj = GetBaseObject(node);
        if (!baseObj || baseObj->SuperClassID() != HELPER_CLASS_ID)
            return;

        MSTR cnBuf;
        const MCHAR* cn = GetObjectClassName(baseObj, cnBuf);
        if (wcsstr(cn, L"CollisionSphere") || wcsstr(cn, L"Wc3CollisionSphere"))
            collisions_.push_back({1, node});
        else if (wcsstr(cn, L"CollisionBox") || wcsstr(cn, L"Wc3CollisionBox"))
            collisions_.push_back({0, node});
    });
}

// ============================================================================
// Collect lights — Wc3Light (LITE) plus stock Max Omni / Spot / Direct
//
// Hidden lights are collected rather than skipped: Evaluate gates `enabled` on
// visibility every frame, so hiding and unhiding one takes effect immediately
// instead of needing a Resync.
// ============================================================================

void MaxSceneAdapter::CollectLights() {
    lights_.clear();
    ForEachSceneNode([&](INode* node) {
        Object* baseObj = GetBaseObject(node);
        if (!baseObj)
            return;

        const Class_ID cid = baseObj->ClassID();
        if (cid == WC3LIGHT_CLASS_ID || cid == NEODEX_LIGHT_CLASS_ID) {
            lights_.push_back({node, true});
            return;
        }

        // Wc3Light is a scripted simpleManipulator, and Max does not reliably
        // route a scripted plugin's declared classID through ClassID() — the
        // collision-shape collector above hits the same wall and resolves by
        // class name. Same fallback here before giving up on a helper.
        if (baseObj->SuperClassID() == HELPER_CLASS_ID) {
            MSTR cnBuf;
            const MCHAR* cn = GetObjectClassName(baseObj, cnBuf);
            if (cn && (wcsstr(cn, L"Wc3Light") || wcsstr(cn, L"Wc3 Light"))) {
                lights_.push_back({node, true});
                return;
            }
        }

        // Stock Max lights. They export to nothing — MDX only has LITE, which
        // Wc3Light authors — but previewing a scene lit with them beats
        // previewing it unlit, and it is what most imported scenes actually
        // carry.
        if (baseObj->SuperClassID() == LIGHT_CLASS_ID)
            lights_.push_back({node, false});
    });

    if (!lights_.empty()) {
        i32 nWc3 = 0;
        for (const auto& li : lights_)
            nWc3 += li.wc3 ? 1 : 0;
        mprintf(_M("  %d light(s): %d Wc3Light, %d stock Max\n"), (i32)lights_.size(), nWc3,
                (i32)lights_.size() - nWc3);
    }
}

// ============================================================================
// GetVNormal helper
// ============================================================================

static Point3 GetVNormal(Mesh& mesh, i32 faceIdx, i32 vertIdx) {
    DWORD smGroup = mesh.faces[faceIdx].getSmGroup();
    if (smGroup == 0)
        return mesh.getFaceNormal(faceIdx);
    Point3 n(0, 0, 0);
    for (i32 f = 0; f < mesh.getNumFaces(); f++) {
        if (mesh.faces[f].getSmGroup() & smGroup)
            for (i32 v = 0; v < 3; v++)
                if (mesh.faces[f].v[v] == (DWORD)vertIdx) {
                    n += mesh.getFaceNormal(f);
                    break;
                }
    }
    return Normalize(n);
}

// ============================================================================
// IModelSource::GetMeshes()
// ============================================================================

std::vector<MeshData> MaxSceneAdapter::GetMeshes() {
    std::vector<MeshData> result;
    for (auto& gs : geosets_) {
        mprintf(_M("    mesh[%d] '%s' ...\n"), gs.geosetId, gs.node->GetName());

        Modifier* skinMod = FindSkinModifier(gs.node);
        // Mirror the exporter's policy (MeshExtractor): evaluate WITH the Skin
        // modifier active at frame 0 — the bind pose by WC3 convention (the
        // editable-mesh state under a Skin can sit in an arbitrary authoring
        // pose on FBX-style rigs).
        ObjectState os = gs.node->EvalWorldState(0);

        if (!os.obj || !os.obj->CanConvertToType(triObjectClassID))
            continue;
        TriObject* triObj = static_cast<TriObject*>(os.obj->ConvertToType(0, triObjectClassID));
        if (!triObj)
            continue;

        Mesh& mesh = triObj->GetMesh();
        mesh.buildNormals();
        i32 numFaces = mesh.getNumFaces();
        i32 numVerts = numFaces * 3;
        gs.expandedVertCount = numVerts;
        gs.faceVertMap.resize(numVerts);

        MeshData md;
        md.geosetId = gs.geosetId;
        md.materialId = gs.materialId;
        md.positions.resize(numVerts);
        md.normals.resize(numVerts);
        md.uvs.resize(numVerts);
        md.indices.resize(numFaces * 3);

        bool hasUVs = mesh.getNumMapVerts(1) > 0;
        // Map channel 2 is the MDX geoset's second UVAS set: Reforged HD
        // unwraps twice and bakes ambient occlusion against the second one,
        // which `hd_ps` samples as ORM.x at TEXCOORD1. The importer writes
        // MDX set 1 there (set 0 goes to channel 1), so mirror that here.
        const bool hasUV1 = mesh.getNumMapVerts(2) > 0 && mesh.mapFaces(2) != nullptr;
        if (hasUV1)
            md.uvs1.resize(numVerts);
        MeshNormalSpec* specN = mesh.GetSpecifiedNormals();
        bool hasSpecN = specN && specN->GetNumNormals() > 0;

        // Object → world at bind time. EvalWorldState hands back OBJECT-space
        // vertices; GetObjectTM(0) = objectOffset × nodeTM brings them to
        // world space including a custom pivot's object offset. The skinning
        // palette applies worldTM(t) × Inverse(nodeTM(0)) per bone, which
        // expects world-space bind vertices — same math as the exporter.
        const Matrix3 objTM = gs.node->GetObjectTM(0);
        Matrix3 normalTM = objTM;
        normalTM.NoTrans();

        for (i32 f = 0; f < numFaces; f++) {
            Face& face = mesh.faces[f];
            for (i32 v = 0; v < 3; v++) {
                i32 outIdx = f * 3 + v;
                i32 origV = face.v[v];
                gs.faceVertMap[outIdx] = origV;

                Point3 pos = mesh.verts[origV] * objTM;
                md.positions[outIdx] = MaxPointToDefault(pos);

                Point3 n = hasSpecN ? specN->GetNormal(f, v) : GetVNormal(mesh, f, origV);
                n = Normalize(n * normalTM);
                md.normals[outIdx] = MaxDirToDefault(n);

                if (hasUVs) {
                    TVFace& tvf = mesh.mapFaces(1)[f];
                    UVVert uv = mesh.mapVerts(1)[tvf.t[v]];
                    md.uvs[outIdx] = {uv.x, 1.0f - uv.y};
                } else {
                    md.uvs[outIdx] = {0, 0};
                }
                if (hasUV1) {
                    TVFace& tvf1 = mesh.mapFaces(2)[f];
                    UVVert uv1 = mesh.mapVerts(2)[tvf1.t[v]];
                    md.uvs1[outIdx] = {uv1.x, 1.0f - uv1.y};
                }
                md.indices[f * 3 + v] = (u32)outIdx;
            }
        }

        if (triObj != os.obj)
            triObj->DeleteThis();

        // Validate skin vertex count
        if (skinMod) {
            ISkin* skin = (ISkin*)skinMod->GetInterface(I_SKIN);
            ISkinContextData* ctx = skin ? skin->GetContextInterface(gs.node) : nullptr;
            i32 skinVerts = ctx ? ctx->GetNumPoints() : -1;
            i32 meshOrigVerts = mesh.getNumVerts();
            if (skinVerts >= 0 && skinVerts != meshOrigVerts)
                mprintf(_M("  *** WARNING Geoset %d: mesh=%d, ISkin=%d MISMATCH!\n"), gs.geosetId,
                        meshOrigVerts, skinVerts);
        }

        mprintf(_M("  Geoset %d: %d expanded, %d faces '%s'%s\n"), gs.geosetId, numVerts, numFaces,
                gs.node->GetName(), skinMod ? _M(" [skinned]") : _M(""));

        result.push_back(std::move(md));
    }
    return result;
}

// ============================================================================
// IModelSource::GetTextures()
// ============================================================================

std::vector<TextureData> MaxSceneAdapter::GetTextures() {
    std::vector<TextureData> result;
    result.reserve(loadedTextures_.size());
    for (auto& lt : loadedTextures_) {
        TextureData td;
        td.textureId = lt.textureId;
        td.replaceableId = lt.replaceableId;
        td.pixels = std::move(lt.rgba);
        // MaxSceneAdapter always produces RGBA8 (3ds Max bitmaps are
        // decoded to 32-bit on import). Apply the engine's filename-
        // suffix-driven sRGB / linear policy on top — `_Diffuse` /
        // `_Emissive` / `_IBL` / default get `_SRGB` SRVs, `_Normal` /
        // `_ORM` / `Textures/Normal` / `Textures/ORM` stay linear.
        // The original asset path is carried on lt.sharedKey for
        // file-backed textures; procedural / sentinel textures have
        // an empty key and get the default-sRGB policy. Mirrors
        // `CImageFile::DetermineImageUsage` @ Preview 0x7ff609bad260.
        td.format = ApplyTextureSrgbPolicy(gfx::Format::R8G8B8A8_UNORM, lt.sharedKey);
        td.width = lt.width;
        td.height = lt.height;
        // Wrap-flag bits (0x1 = WrapWidth, 0x2 = WrapHeight) are lifted at
        // LoadTexture time from the source texmap — Wc3Bitmap's wrapU/wrapV
        // PB2 booleans or plain BitmapTex's StdUVGen U_WRAP/V_WRAP. The
        // renderer's sampler manager picks the matching wrap variant.
        td.wrapFlags = lt.wrapFlags;
        // sharedKey was stamped at LoadTexture time (file-backed slots
        // only — replaceable / sentinel paths leave it empty so they
        // stay per-model). When pixels are also empty the renderer treats
        // this as a borrow-only entry and goes through BindShared.
        td.sharedKey = std::move(lt.sharedKey);
        result.push_back(std::move(td));
    }
    loadedTextures_.clear();
    return result;
}

// ============================================================================
// IModelSource::GetMaterials()
// ============================================================================

std::vector<MaterialData> MaxSceneAdapter::GetMaterials() {
    std::vector<MaterialData> result;
    for (auto& mi : materials_) {
        MaterialData md;
        md.materialId = mi.materialId;
        md.priorityPlane = mi.priorityPlane;
        md.sortOrder = mi.sortOrder;
        for (auto& li : mi.layers) {
            MaterialLayerData ld;
            ld.filterMode = li.filterMode;
            ld.textureId = li.textureId;
            ld.alpha = li.alpha;
            ld.flags = li.flags;
            ld.shaderId = li.shaderId;
            ld.normalMapId = li.normalMapId;
            ld.ormMapId = li.ormMapId;
            ld.emissiveMapId = li.emissiveMapId;
            ld.teamColorMapId = li.teamColorMapId;
            ld.emissiveGain = li.emissiveGain;
            ld.fresnelOpacity = li.fresnelOpacity;
            ld.fresnelTeamColor = li.fresnelTeamColor;
            ld.fresnelColor = li.fresnelColor;
            ld.textureAnimationId = li.textureAnimationId;
            md.layers.push_back(ld);
        }
        result.push_back(std::move(md));
    }
    return result;
}

// ============================================================================
// IModelSource::GetSkeleton()
// ============================================================================

SkeletonData MaxSceneAdapter::GetSkeleton() {
    SkeletonData sd;
    sd.nodeCount = (i32)bones_.size();
    sd.inverseBindMatrices.resize(sd.nodeCount);
    sd.billboardFlags.resize(sd.nodeCount, 0);
    sd.nodeParents.assign(sd.nodeCount, -1);

    // Helper: "Billboarded" & family are ints written via SetUserProp; treat
    // a non-zero value as on.
    auto userFlag = [](INode* node, const TCHAR* name) {
        i32 val = 0;
        return node->GetUserPropInt(name, val) && val != 0;
    };

    for (i32 i = 0; i < sd.nodeCount; i++) {
        INode* node = bones_[i].node;
        Matrix3 inv = Inverse(node->GetNodeTM(0));
        bones_[i].inverseBind = inv;
        sd.inverseBindMatrices[i] = PackMatrix(inv);

        sd.billboardFlags[i] = PackBillboardFlags(
            userFlag(node, _T("Billboarded")), userFlag(node, _T("BillboardedLockX")),
            userFlag(node, _T("BillboardedLockY")), userFlag(node, _T("BillboardedLockZ")),
            userFlag(node, _T("CameraAnchored")));
    }
    return sd;
}

// ============================================================================
// IModelSource::GetSkinWeights()
// ============================================================================

std::vector<SkinWeightData> MaxSceneAdapter::GetSkinWeights() {
    std::vector<SkinWeightData> result;

    for (auto& gs : geosets_) {
        if (gs.expandedVertCount == 0 || gs.faceVertMap.empty())
            continue;
        Modifier* skinMod = FindSkinModifier(gs.node);
        if (!skinMod) {
            // Rigid bind to the mesh's own node-bone (registered by
            // CollectBones): every vertex weighted 1.0 to local palette
            // slot 0, whose subset entry is the node's global bone index.
            auto it = boneNodeToIdx_.find(gs.node);
            if (it == boneNodeToIdx_.end()) {
                mprintf(_M("  Geoset %d: no Skin modifier and no node bone\n"), gs.geosetId);
                continue;
            }
            SkinWeightData sw;
            sw.geosetId = gs.geosetId;
            sw.influences.resize(gs.expandedVertCount);
            for (auto& inf : sw.influences) {
                inf.boneIdx[0] = 0;
                inf.weight[0] = 1.0f;
            }
            sw.subsetNodeIndices.push_back(it->second);
            mprintf(_M("  Geoset %d: unskinned, rigid-bound to node bone %d\n"), gs.geosetId,
                    it->second);
            result.push_back(std::move(sw));
            continue;
        }
        ISkin* skin = (ISkin*)skinMod->GetInterface(I_SKIN);
        ISkinContextData* ctx = skin ? skin->GetContextInterface(gs.node) : nullptr;
        if (!ctx) {
            mprintf(_M("  Geoset %d: ISkin context failed\n"), gs.geosetId);
            continue;
        }

        i32 origVC = ctx->GetNumPoints();
        struct OW {
            i32 bi[4] = {0, 0, 0, 0};
            f32 wt[4] = {0, 0, 0, 0};
        };
        std::vector<OW> ow(origVC);
        i32 zeroWeightVerts = 0;
        for (i32 v = 0; v < origVC; v++) {
            i32 nw = ctx->GetNumAssignedBones(v);
            f32 totalW = 0;
            for (i32 b = 0; b < std::min(nw, 4); b++) {
                INode* bn = skin->GetBone(ctx->GetAssignedBone(v, b));
                i32 idx = 0;
                if (bn) {
                    auto it = boneNodeToIdx_.find(bn);
                    if (it != boneNodeToIdx_.end())
                        idx = it->second;
                }
                ow[v].bi[b] = idx;
                ow[v].wt[b] = ctx->GetBoneWeight(v, b);
                totalW += ow[v].wt[b];
            }
            if (totalW < 0.001f)
                zeroWeightVerts++;
        }

        i32 ec = gs.expandedVertCount;
        SkinWeightData sw;
        sw.geosetId = gs.geosetId;
        sw.influences.resize(ec);
        i32 outOfRange = 0, mapped = 0, maxFVM = 0;
        for (i32 vi = 0; vi < ec; vi++) {
            i32 origV = gs.faceVertMap[vi];
            if (origV > maxFVM)
                maxFVM = origV;
            if (origV >= 0 && origV < origVC) {
                for (i32 j = 0; j < 4; j++) {
                    sw.influences[vi].boneIdx[j] = ow[origV].bi[j];
                    sw.influences[vi].weight[j] = ow[origV].wt[j];
                }
                mapped++;
            } else {
                outOfRange++;
            }
        }
        mprintf(
            _M("  Geoset %d: skin %d expanded, %d ISkin, maxIdx=%d, mapped=%d, OOB=%d, zeroW=%d\n"),
            gs.geosetId, ec, origVC, maxFVM, mapped, outOfRange, zeroWeightVerts);

        // Build per-geoset compact subset palette required by the new
        // per-geoset palette system (commit 6c23683). boneIdx values above
        // are global node indices; remap them to LOCAL slots so that
        // GeosetPaletteSize() > 0 and bonePaletteCb gets created.
        {
            std::unordered_map<i32, i32> globalToLocal;
            for (auto& inf : sw.influences) {
                for (i32 j = 0; j < 4; j++) {
                    if (inf.weight[j] > 0.0f) {
                        i32 g = inf.boneIdx[j];
                        if (globalToLocal.find(g) == globalToLocal.end()) {
                            i32 local = (i32)sw.subsetNodeIndices.size();
                            globalToLocal.emplace(g, local);
                            sw.subsetNodeIndices.push_back(g);
                        }
                    }
                }
            }
            for (auto& inf : sw.influences) {
                for (i32 j = 0; j < 4; j++) {
                    auto it = globalToLocal.find(inf.boneIdx[j]);
                    inf.boneIdx[j] = (it != globalToLocal.end()) ? it->second : 0;
                }
            }
        }

        result.push_back(std::move(sw));
    }
    return result;
}

// ============================================================================
// IModelSource::GetParticleConfigs()
// ============================================================================

std::vector<ParticleEmitterConfig> MaxSceneAdapter::GetParticleConfigs() {
    std::vector<ParticleEmitterConfig> result;
    for (auto& pi : particles_) {
        Object* obj = GetBaseObject(pi.node);
        if (!obj)
            continue;

        ParticleEmitterConfig cfg;
        cfg.textureId = pi.textureId >= 0 ? pi.textureId : 0;
        cfg.replaceableId = pi.replaceableId;
        cfg.filterMode = MapPE2BlendMode(PB2IntOr(obj, L"BlendMode", 0, 0));

        cfg.rows = PB2IntOr(obj, L"TextureRows", 0, cfg.rows);
        cfg.cols = PB2IntOr(obj, L"TextureCols", 0, cfg.cols);
        cfg.unshaded = PB2BoolOr(obj, L"Unshaded", 0, cfg.unshaded);
        cfg.lifeSpan = PB2FloatOr(obj, L"Life", 0, cfg.lifeSpan);
        cfg.squirt = PB2BoolOr(obj, L"Squirt", 0, cfg.squirt);

        // Segment colors (Point3 in Wc3Particles2 → Vector3f).
        Color cv;
        if (PB2Color(obj, L"ColorStart", 0, cv))
            cfg.startColor = {cv.r, cv.g, cv.b};
        if (PB2Color(obj, L"ColorMid", 0, cv))
            cfg.midColor = {cv.r, cv.g, cv.b};
        if (PB2Color(obj, L"ColorEnd", 0, cv))
            cfg.endColor = {cv.r, cv.g, cv.b};

        // Segment alpha (0-255 int → float) / scale / mid-time.
        cfg.startAlpha = (f32)PB2IntOr(obj, L"AlphaStart", 0, (i32)cfg.startAlpha);
        cfg.midAlpha = (f32)PB2IntOr(obj, L"AlphaMid", 0, (i32)cfg.midAlpha);
        cfg.endAlpha = (f32)PB2IntOr(obj, L"AlphaEnd", 0, (i32)cfg.endAlpha);
        cfg.startScale = PB2FloatOr(obj, L"ScaleStart", 0, cfg.startScale);
        cfg.midScale = PB2FloatOr(obj, L"ScaleMid", 0, cfg.midScale);
        cfg.endScale = PB2FloatOr(obj, L"ScaleEnd", 0, cfg.endScale);
        cfg.midTime = PB2FloatOr(obj, L"MidTime", 0, cfg.midTime);

        // ParticleType: Wc3Particles2 0=Head,1=Tail,2=Both → renderer 1=Head,2=Tail,3=Both.
        if (i32 pt; PB2Int(obj, L"ParticleType", 0, pt))
            cfg.particleType = pt + 1;
        cfg.tailLength = PB2FloatOr(obj, L"TailLength", 0, cfg.tailLength);

        cfg.modelSpace = PB2BoolOr(obj, L"ModelSpace", 0, cfg.modelSpace);
        cfg.xyQuad = PB2BoolOr(obj, L"XYQuad", 0, cfg.xyQuad);
        cfg.lineEmitter = PB2BoolOr(obj, L"LineEmitter", 0, cfg.lineEmitter);

        // Head/tail UV animation frames.
        cfg.headLifeStart = PB2IntOr(obj, L"HeadLifeStart", 0, cfg.headLifeStart);
        cfg.headLifeEnd = PB2IntOr(obj, L"HeadLifeEnd", 0, cfg.headLifeEnd);
        cfg.headLifeRepeat = PB2IntOr(obj, L"HeadLifeRepeat", 0, cfg.headLifeRepeat);
        cfg.headDecayStart = PB2IntOr(obj, L"HeadDecayStart", 0, cfg.headDecayStart);
        cfg.headDecayEnd = PB2IntOr(obj, L"HeadDecayEnd", 0, cfg.headDecayEnd);
        cfg.headDecayRepeat = PB2IntOr(obj, L"HeadDecayRepeat", 0, cfg.headDecayRepeat);
        cfg.tailLifeStart = PB2IntOr(obj, L"TailLifeStart", 0, cfg.tailLifeStart);
        cfg.tailLifeEnd = PB2IntOr(obj, L"TailLifeEnd", 0, cfg.tailLifeEnd);
        cfg.tailLifeRepeat = PB2IntOr(obj, L"TailLifeRepeat", 0, cfg.tailLifeRepeat);
        cfg.tailDecayStart = PB2IntOr(obj, L"TailDecayStart", 0, cfg.tailDecayStart);
        cfg.tailDecayEnd = PB2IntOr(obj, L"TailDecayEnd", 0, cfg.tailDecayEnd);
        cfg.tailDecayRepeat = PB2IntOr(obj, L"TailDecayRepeat", 0, cfg.tailDecayRepeat);

        cfg.sortZ = PB2BoolOr(obj, L"SortPrimitives", 0, cfg.sortZ);
        cfg.unfogged = PB2BoolOr(obj, L"Unfogged", 0, cfg.unfogged);
        cfg.count = PB2IntOr(obj, L"Count", 0, cfg.count);
        cfg.priorityPlane = PB2IntOr(obj, L"PriorityPlane", 0, cfg.priorityPlane);

        mprintf(_M("  Particle %d: '%s' tex=%d fm=%d unshaded=%d\n"), pi.emitterId,
                pi.node->GetName(), pi.textureId, cfg.filterMode, (i32)cfg.unshaded);
        result.push_back(cfg);
    }
    return result;
}

// ============================================================================
// IModelSource::GetRibbonConfigs()
// ============================================================================

std::vector<RibbonEmitterConfig> MaxSceneAdapter::GetRibbonConfigs() {
    std::vector<RibbonEmitterConfig> result;
    for (auto& ri : ribbons_) {
        Object* obj = GetBaseObject(ri.node);
        if (!obj)
            continue;

        RibbonEmitterConfig cfg;
        cfg.textureId = ri.textureId >= 0 ? ri.textureId : 0;

        // Wc3Ribbon has no filterMode param of its own; derive from the node
        // material when it's a Wc3Material, otherwise fall back to Additive.
        if (Mtl* mtl = ri.node->GetMtl(); mtl && mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
            cfg.filterMode = MapFilterMode(PB2IntOr(mtl, L"filterMode", 0, 1) - 1);
        } else {
            cfg.filterMode = MapFilterMode(3);
        }

        cfg.rows = PB2IntOr(obj, L"Texture Rows", 0, cfg.rows);
        cfg.cols = PB2IntOr(obj, L"Texture Columns", 0, cfg.cols);
        cfg.emission = (f32)PB2IntOr(obj, L"Edges Per Second", 0, (i32)cfg.emission);
        cfg.life = PB2FloatOr(obj, L"Edge Lifetime", 0, cfg.life);
        cfg.gravity = PB2FloatOr(obj, L"Gravity", 0, cfg.gravity);
        // Wc3Ribbon has no unshaded/twosided params — engine renders ribbons
        // double-sided + unshaded by convention.
        cfg.unshaded = true;
        cfg.twoSided = true;

        mprintf(_M("  Ribbon %d: '%s' tex=%d fm=%d\n"), ri.emitterId, ri.node->GetName(),
                ri.textureId, cfg.filterMode);
        result.push_back(cfg);
    }
    return result;
}

// ============================================================================
// IModelSource::GetCollisionShapes()
// ============================================================================

std::vector<CollisionShapeData> MaxSceneAdapter::GetCollisionShapes() {
    std::vector<CollisionShapeData> result;
    for (auto& ci : collisions_) {
        Object* obj = GetBaseObject(ci.node);
        if (!obj)
            continue;

        CollisionShapeData cs;
        cs.type = ci.type;
        cs.radius = 0;
        cs.vertices[0] = {0, 0, 0};
        cs.vertices[1] = {0, 0, 0};

        if (ci.type == 1) {
            cs.radius = PB2FloatOr(obj, L"radius", 0, 10.0f);
        } else {
            // Extent parameters are full width/length/height; bounding corners
            // use half-extents in X/Y and full height in Z.
            const f32 halfW = PB2FloatOr(obj, L"width", 0, 5.0f) * 0.5f;
            const f32 halfL = PB2FloatOr(obj, L"length", 0, 5.0f) * 0.5f;
            const f32 h = PB2FloatOr(obj, L"height", 0, 5.0f);
            cs.vertices[0] = {-halfW, -halfL, 0};
            cs.vertices[1] = {halfW, halfL, h};
        }
        result.push_back(cs);
    }
    return result;
}

// ============================================================================
// IModelSource::GetPE1Configs()
// ============================================================================

std::vector<AttachmentConfig> MaxSceneAdapter::GetAttachmentConfigs() {
    std::vector<AttachmentConfig> result;
    for (auto& ai : attachments_) {
        AttachmentConfig cfg;
        cfg.attachmentId = ai.attachmentId;
        cfg.modelPath = ai.modelPath;
        result.push_back(cfg);
    }
    return result;
}

std::vector<PE1EmitterConfig> MaxSceneAdapter::GetPE1Configs() {
    std::vector<PE1EmitterConfig> result;
    for (auto& pi : pe1Emitters_) {
        Object* obj = GetBaseObject(pi.node);
        if (!obj)
            continue;
        PE1EmitterConfig cfg;
        cfg.modelPath = pi.modelPath;
        cfg.lifespan = PB2FloatOr(obj, L"Life", 0, cfg.lifespan);
        cfg.scale = PB2FloatOr(obj, L"Scale", 0, cfg.scale);
        result.push_back(cfg);
    }
    return result;
}

// ============================================================================
// IModelSource::GetCornEmitterInits() — BlizzPopcorn → CornEffectsEmitter.
// Static config only; per-frame multipliers + transform ship via
// FrameState::cornStates in Evaluate.
// ============================================================================

std::vector<CornEmitterInit> MaxSceneAdapter::GetCornEmitterInits() {
    std::vector<CornEmitterInit> result;
    result.reserve(popcornEmitters_.size());
    for (auto& pi : popcornEmitters_) {
        Object* obj = GetBaseObject(pi.node);
        if (!obj)
            continue;
        CornEmitterInit init;
        init.emitterId = pi.emitterId;
        init.pkbPath = pi.pkbPath;
        // Deliberately NOT forwarding pi.animVisibilityGuide. The guide is
        // evaluated adapter-side in Evaluate() (EvaluateGuideForSequence
        // against sequenceRanges_) and shipped as CornFrameState::visibility,
        init.animVisibilityGuide.clear();
        init.replaceableId = pi.replaceableId;
        mprintf(_M("  [Popcorn emitterId=%d] pkb='%hs' guide='%hs' replId=%d\n"),
                pi.emitterId, pi.pkbPath.c_str(),
                pi.animVisibilityGuide.c_str(), pi.replaceableId);
        init.defaultLifeSpan = PB2FloatOr(obj, L"lifeSpan", 0, 0.0f);
        init.defaultEmissionRate = PB2FloatOr(obj, L"emissionRate", 0, 0.0f);
        init.defaultSpeed = PB2FloatOr(obj, L"speed", 0, 0.0f);

        Vector4f col{1, 1, 1, 1};
        Color cv;
        if (PB2Color(obj, L"baseColor", 0, cv)) {
            col.x = cv.r;
            col.y = cv.g;
            col.z = cv.b;
        }
        col.w = PB2FloatOr(obj, L"alpha", 0, 1.0f);
        init.defaultColor = col;

        // PopcornScaling (NodeFlag 0x40000) is now authored as the
        // `flagScaling` bool on the Wc3Popcorn helper — read by
        // CollectParticleEmitters and parked on PopcornEmitterInfo.
        init.cornEffectsScaling = pi.cornEffectsScaling;

        result.push_back(std::move(init));
    }
    return result;
}

// ============================================================================
// IAnimationSource::Evaluate() — compute per-frame state from Max scene.
// Max controls the timeline, so everything in the PoseRequest but the primary
// clip's timeMs is unused here: there is only ever one clip (Max's own
// timeline), global-sequence time tracks it, and the world / camera transforms
// come from the viewport rather than the request.
// ============================================================================

FrameState MaxSceneAdapter::Evaluate(const PoseRequest& req) const {
    const i32 timeMs = req.PrimaryClip().timeMs;

    // Convert ms → Max ticks (0 if the tick rate is unavailable).
    const i32 tpf = GetTicksPerFrame(), fps = GetFrameRate();
    const TimeValue t =
        (tpf > 0 && fps > 0) ? (TimeValue)((f32)timeMs * (f32)fps / 1000.0f * (f32)tpf) : 0;
    constexpr f32 kDegToRad = std::numbers::pi_v<f32> / 180.0f;

    FrameState state;

    // Bone world matrices
    state.boneWorldMatrices.resize(bones_.size());
    for (usize i = 0; i < bones_.size(); i++)
        state.boneWorldMatrices[i] = PackMatrix(bones_[i].node->GetNodeTM(t));

    // Geoset transforms + visibility + per-vertex-mod color
    const i32 gc = (i32)geosets_.size();
    state.geosetTransforms.resize(gc, Matrix44f::identity());
    state.geosetAlphas.assign(gc, 1.0f);
    state.geosetColors.assign(gc, Vector3f{1, 1, 1});
    for (i32 i = 0; i < gc; i++) {
        INode* node = geosets_[i].node;
        if (!node)
            continue;
        state.geosetTransforms[i] = PackMatrix(node->GetNodeTM(t));
        // Geoset alpha carries visibility only; per-layer opacity ships
        // separately via layerAlphas so each layer can fade independently.
        state.geosetAlphas[i] = std::clamp(node->GetVisibility(t), 0.0f, 1.0f);

        // Wc3VertexMod is a MaxScript scripted plugin. Max does NOT route
        // its declared classID through Modifier::ClassID() — the C++ value
        // is some opaque internal id. The exporter hits the same wall and
        // resolves it by name (geoset_anim_extractor.cpp::isWc3VertexMod).
        static const wchar_t* kWc3VertexModNames[] = {
            L"Wc3VertexMod",     // MaxScript classOf observed
            L"Wdx_Wc3VertexMod", // raw plugin internal name
            L"Wc3 Vertex Color", // plugin "name:" attribute
            nullptr};
        if (Modifier* mod = FindModifierByClassName(node, kWc3VertexModNames)) {
            if (PB2BoolOr(mod, L"UsesColor", t, false)) {
                Color col(1, 1, 1);
                PB2Color(mod, L"VertexColor", t, col);
                state.geosetColors[i] = {col.r, col.g, col.b};
            }
        }
    }

    // Per-layer alpha: one entry per unique (material, layer) authored on any
    // geoset's node material.
    {
        std::vector<i32> sentMats;
        for (auto& gs : geosets_) {
            Mtl* mtl = gs.node ? gs.node->GetMtl() : nullptr;
            if (!mtl)
                continue;
            auto it = mtlToId_.find(mtl);
            if (it == mtlToId_.end())
                continue;
            const i32 matId = it->second;
            if (std::find(sentMats.begin(), sentMats.end(), matId) != sentMats.end())
                continue;
            sentMats.push_back(matId);
            ForEachWc3SubMtl(mtl, [&](Mtl* sub, i32 layerIdx) {
                const f32 a = PB2FloatOr(sub, L"opacity", t, 100.0f);
                state.layerAlphas.push_back({matId, layerIdx, std::min(a / 100.0f, 1.0f)});
            });
        }
    }

    // Particle emitter states
    for (auto& pi : particles_) {
        Object* obj = GetBaseObject(pi.node);
        if (!obj)
            continue;
        FrameState::ParticleFrameState ps;
        ps.emitterId = pi.emitterId;
        ps.transform = PackMatrix(pi.node->GetNodeTM(t));
        ps.emissionRate = PB2FloatOr(obj, L"EmissionRate", t);
        ps.speed = PB2FloatOr(obj, L"Speed", t);
        ps.variation = PB2FloatOr(obj, L"Variation", t);
        ps.coneAngle = PB2FloatOr(obj, L"ConeAngle", t) * kDegToRad; // deg→rad
        ps.gravity = PB2FloatOr(obj, L"Gravity", t);
        // Swap on the Blizzard lift: Wc3Particle2's "Width" / "Height" params
        // are authored along Max's X / Y axes, which map to Blizzard's length /
        // width respectively (Blizzard +X = forward = length, +Y = left = width).
        ps.width = PB2FloatOr(obj, L"Height", t);
        ps.length = PB2FloatOr(obj, L"Width", t);
        ps.visibility = pi.node->GetVisibility(t);
        ps.squirting = PB2BoolOr(obj, L"Squirt", t, false);
        state.particleStates.push_back(ps);
    }

    // Ribbon emitter states
    for (auto& ri : ribbons_) {
        Object* obj = GetBaseObject(ri.node);
        if (!obj)
            continue;
        FrameState::RibbonFrameState rs;
        rs.emitterId = ri.emitterId;
        rs.transform = PackMatrix(ri.node->GetNodeTM(t));
        rs.above = PB2FloatOr(obj, L"Height Above", t, 20.0f);
        rs.below = PB2FloatOr(obj, L"Height Below", t, 20.0f);
        rs.alpha = PB2FloatOr(obj, L"Alpha", t, 1.0f);
        rs.visibility = ri.node->GetVisibility(t);
        rs.slot = PB2IntOr(obj, L"Texture Slot", t, 0);
        Color cv;
        if (PB2Color(obj, L"Color", t, cv))
            rs.color = {cv.r, cv.g, cv.b};
        else
            rs.color = {1, 1, 1};
        state.ribbonStates.push_back(rs);
    }

    // Scene lights.
    //
    // Wc3Light's parameter names do not match its UI labels — `ShadowColor` /
    // `ShadowValue` are the *primary* colour and intensity and `AmbColor` /
    // `AmbValue` the ambient pair, not the other way round. This mirrors the
    // exporter's mapping (wc3_light_extractor.cpp) exactly; reading them the
    // way the labels suggest would light the preview differently from the
    // model that gets written out.
    //
    // Conventions on the renderer side follow MdxModelAdapter::Evaluate:
    // diffuse is colour premultiplied by intensity, only Omni is positional
    // (WC3's CreateLight_1 runs Directional *and* Ambient through the
    // directional path), and a non-positional light's direction is its node's
    // local -Z.
    for (const auto& li : lights_) {
        INode* node = li.node;
        if (!node)
            continue;
        Object* obj = GetBaseObject(node);
        if (!obj)
            continue;

        FrameState::LightState ls;
        const Matrix44f world = PackMatrix(node->GetNodeTM(t));
        const bool visible = !node->IsNodeHidden() && node->GetVisibility(t) > 0.0f;

        Vector3f color{1.0f, 1.0f, 1.0f};
        f32 intensity = 0.0f;

        if (li.wc3) {
            // The plugin's dropdown is 1-based (1=Omni, 2=Directional,
            // 3=Ambient); MDX is 0-based. Same remap the exporter does.
            switch (PB2IntOr(obj, L"LightType", t, 1)) {
            case 2:
                ls.kind = FrameState::LightKind::Directional;
                break;
            case 3:
                ls.kind = FrameState::LightKind::Ambient;
                break;
            default:
                ls.kind = FrameState::LightKind::Omni;
                break;
            }

            Color c(1.0f, 1.0f, 1.0f);
            if (PB2Color(obj, L"ShadowColor", t, c))
                color = {c.r, c.g, c.b};
            intensity = PB2FloatOr(obj, L"ShadowValue", t, 0.0f);

            // SetLightColor clamps the ambient track to 0..1 and
            // SetLightIntensity floors both intensities at 0
            // (Engine/Source/Anim/Anim.cpp), so do the same here.
            Color amb(0.0f, 0.0f, 0.0f);
            if (PB2Color(obj, L"AmbColor", t, amb)) {
                ls.ambientColor = {std::clamp(amb.r, 0.0f, 1.0f), std::clamp(amb.g, 0.0f, 1.0f),
                                   std::clamp(amb.b, 0.0f, 1.0f)};
            }
            ls.ambIntensity = std::max(0.0f, PB2FloatOr(obj, L"AmbValue", t, 0.0f));
            ls.attenStart = PB2FloatOr(obj, L"DecayStart", t, 0.0f);
            ls.attenEnd = PB2FloatOr(obj, L"DecayEnd", t, 0.0f);
            ls.enabled = visible;

            // Reforged / 3.0 parameters. Read at frame 0 rather than `t`: the
            // game's SetLightValues uses only the static values and never
            // evaluates KLSS / KLSE / KLQF / KLLF / KLDA, MdxModelAdapter
            // mirrors that, and frame 0 is the static value the exporter
            // writes. A light saved before the plug-in grew these parameters
            // falls back to the LightState defaults, which are the game's own.
            ls.shadowIntensity = PB2FloatOr(obj, L"ShadowIntensity", 0, ls.shadowIntensity);
            ls.shadowCasting = PB2BoolOr(obj, L"ShadowCasting", 0, ls.shadowCasting);
            ls.shadowCastingStart =
                PB2FloatOr(obj, L"ShadowCastingStart", 0, ls.shadowCastingStart);
            ls.shadowCastingEnd = PB2FloatOr(obj, L"ShadowCastingEnd", 0, ls.shadowCastingEnd);
            ls.quadraticFalloff = PB2FloatOr(obj, L"QuadraticFalloff", 0, ls.quadraticFalloff);
            ls.linearFalloff = PB2FloatOr(obj, L"LinearFalloff", 0, ls.linearFalloff);
            ls.damping = PB2FloatOr(obj, L"Damping", 0, ls.damping);
        } else {
            // Stock Max light. SuperClassID() == LIGHT_CLASS_ID is the SDK's
            // guarantee that this derives from LightObject; Type() lives one
            // level down on GenLight, which a third-party or photometric light
            // need not be — hence the dynamic_cast and the Omni default.
            auto* lo = static_cast<LightObject*>(obj);
            i32 type = OMNI_LIGHT;
            if (auto* gl = dynamic_cast<GenLight*>(obj))
                type = gl->Type();
            // A spot previews as a point light at its apex: LightState has no
            // cone, so Omni is the closest honest mapping. Only the two
            // parallel types become Directional.
            ls.kind = (type == DIR_LIGHT || type == TDIR_LIGHT)
                          ? FrameState::LightKind::Directional
                          : FrameState::LightKind::Omni;

            const Point3 c = lo->GetRGBColor(t);
            color = {c.x, c.y, c.z};
            intensity = lo->GetIntensity(t);
            // No MDX-style ambient pair on a Max light, so it contributes a
            // diffuse term only.
            if (lo->GetUseAtten()) {
                ls.attenStart = lo->GetAtten(t, LIGHT_ATTEN_START);
                ls.attenEnd = lo->GetAtten(t, LIGHT_ATTEN_END);
            }
            ls.enabled = visible && lo->GetUseLight();
        }

        intensity = std::max(0.0f, intensity);
        ls.diffuse = {color.x * intensity, color.y * intensity, color.z * intensity};
        ls.dirIntensity = intensity;

        // The game gives a light whose node is named Key_ShadowCast the first
        // point-shadow slot (EnvSet field 13). Same test MdxModelAdapter makes
        // on the MDX node name, and the Max node name is what gets exported.
        if (const MCHAR* nodeName = node->GetName())
            ls.shadowPriority = wcsstr(nodeName, L"Key_ShadowCast") != nullptr;

        if (ls.kind == FrameState::LightKind::Omni)
            ls.worldPos = whiteout::transform_point(Vector3f{0.0f, 0.0f, 0.0f}, world);
        else
            ls.worldDir = whiteout::transform_normal(Vector3f{0.0f, 0.0f, -1.0f}, world);

        state.lights.push_back(ls);
    }

    // Collision transforms
    for (auto& ci : collisions_)
        state.collisionTransforms.push_back(PackMatrix(ci.node->GetNodeTM(t)));

    // Attachment transforms
    for (usize i = 0; i < attachments_.size(); i++) {
        auto& ai = attachments_[i];
        state.attachmentStates.push_back(
            {(i32)i, PackMatrix(ai.node->GetNodeTM(t)), ai.node->GetVisibility(t)});
    }

    // PE1 emitter states
    for (auto& pi : pe1Emitters_) {
        Object* obj = GetBaseObject(pi.node);
        if (!obj)
            continue;
        FrameState::PE1FrameState ps;
        ps.emitterId = pi.emitterId;
        ps.transform = PackMatrix(pi.node->GetNodeTM(t));
        ps.speed = PB2FloatOr(obj, L"Speed", t);
        ps.emissionRate = PB2FloatOr(obj, L"EmissionRate", t);
        ps.latitude = PB2FloatOr(obj, L"Latitude", t) * kDegToRad;
        ps.longitude = PB2FloatOr(obj, L"Longitude", t) * kDegToRad;
        ps.gravity = PB2FloatOr(obj, L"Gravity", t);
        ps.visibility = pi.node->GetVisibility(t);
        state.pe1States.push_back(ps);
    }

    // BlizzPopcorn (corn emitter) states. Mirrors MdxModelAdapter::Evaluate:
    // pull a uniform scale off the matrix, normalize the rows, then apply
    // the engine's 90° Z spawn-frame rotation. Per-frame multipliers come
    // from the same PB2 fields used at registration so animated channels
    // (Wc3Particles2-style scrubbing) propagate through.
    if (!popcornEmitters_.empty()) {
        // Find which WdxSequenceManager sequence (if any) contains the
        // current Max time, then gate each emitter's visibility against
        // its animVisibilityGuide using that name. Inclusive-end match
        // mirrors how the renderer's frame ticker treats sequence ranges.
        std::string currentSequenceName;
        for (const auto& r : sequenceRanges_) {
            if (timeMs >= r.startMs && timeMs <= r.endMs) {
                currentSequenceName = r.name;
                break;
            }
        }

        const Matrix44f kCornFxSpawnFrameRotation =
            Matrix44f::rotation_z(1.5707963267948966f);
        for (auto& pi : popcornEmitters_) {
            Object* obj = GetBaseObject(pi.node);
            if (!obj)
                continue;
            FrameState::CornFrameState cs;
            cs.emitterId = pi.emitterId;

            Matrix44f baseM = PackMatrix(pi.node->GetNodeTM(t));
            auto rowMag = [&](i32 r) {
                const f32 x = baseM.data[r][0];
                const f32 y = baseM.data[r][1];
                const f32 z = baseM.data[r][2];
                return std::sqrt(x * x + y * y + z * z);
            };
            const f32 sX = rowMag(0);
            const f32 sY = rowMag(1);
            const f32 sZ = rowMag(2);
            cs.scale = (sX + sY + sZ) / 3.0f;
            auto normalizeRow = [&](i32 r, f32 mag) {
                if (mag > 1.0e-6f) {
                    const f32 inv = 1.0f / mag;
                    baseM.data[r][0] *= inv;
                    baseM.data[r][1] *= inv;
                    baseM.data[r][2] *= inv;
                }
            };
            normalizeRow(0, sX);
            normalizeRow(1, sY);
            normalizeRow(2, sZ);
            cs.transform = kCornFxSpawnFrameRotation * baseM;

            cs.lifeSpanMul = PB2FloatOr(obj, L"lifeSpan", t, 1.0f);
            cs.emissionRateMul = PB2FloatOr(obj, L"emissionRate", t, 1.0f);
            cs.speedMul = PB2FloatOr(obj, L"speed", t, 1.0f);

            Vector4f col{1, 1, 1, 1};
            Color cv;
            if (PB2Color(obj, L"baseColor", t, cv)) {
                col.x = cv.r;
                col.y = cv.g;
                col.z = cv.b;
            }
            col.w = PB2FloatOr(obj, L"alpha", t, 1.0f);
            cs.color = col;
            // NOT node->GetVisibility(t): a corn emitter's own visibility
            // track is dead data. The engine parses KPPV and never reads it
            // (MdxModelAdapter says the same and sets cs.visibility purely
            // from gateByBoneAncestors, unlike the ribbon / light / PE1 / PE2
            // states right above, which all multiply their visibilityTracks
            // in). Honouring the Max node's track here made the preview hide
            // an emitter the engine would have kept spawning — and the MDLX
            // importer materialises KPPV as exactly such a track, so a
            // round-tripped Reforged model walks straight into it.
            //
            // The animVisibilityGuide is the only thing that may gate a corn
            // emitter, and on this path it is evaluated adapter-side (see
            // GetCornEmitterInits for why the renderer's own guide gate is
            // inert here). Visibility 0 makes ApplyCornFrameStates set
            // OwningAgentVisible=false, which is the same `active` edge the
            // engine's SystemDead bit takes.
            cs.visibility = 1.0f;
            if (!EvaluateGuideForSequence(pi.animVisibilityGuide, currentSequenceName))
                cs.visibility = 0.0f;

            state.cornStates.push_back(cs);
        }
    }

    // Texture animations (TXAN): compose one UV matrix per registered
    // source into FrameState::texAnimMatrices — the palette the WC3 geoset
    // passes read through the layer's textureAnimationId (see
    // ApplyTexAnimPaletteToFrame; the old FrameState::texAnims channel fed
    // RenderModel::matTexAnim, which nothing reads). Values convert
    // Max → MDX exactly as the exporter writes them (KTAT.x = -U_Offset,
    // KTAT.y = V_Offset, KTAR = anim_WAngle degrees CCW around the texture
    // centre, KTAS = tiling) and the matrix is composed exactly like
    // MdxModelAdapter's TXAN build, so preview == exported file.
    for (i32 taId = 0; taId < (i32)uvAnimSources_.size(); taId++) {
        const auto& src = uvAnimSources_[taId];
        f32 uOff = 0, vOff = 0, uTile = 1, vTile = 1, angDeg = 0;
        if (src.texmap && src.texmap->ClassID() == WC3_BITMAP_CLASS_ID) {
            PB2Float(src.texmap, L"anim_UOffset", t, uOff);
            PB2Float(src.texmap, L"anim_VOffset", t, vOff);
            PB2Float(src.texmap, L"anim_UTiling", t, uTile);
            PB2Float(src.texmap, L"anim_VTiling", t, vTile);
            PB2Float(src.texmap, L"anim_WAngle", t, angDeg);
        } else if (BitmapTex* bmt = UnwrapBitmapTex(src.texmap)) {
            if (StdUVGen* uvg = bmt->GetUVGen()) {
                uOff = uvg->GetUOffs(t);
                vOff = uvg->GetVOffs(t);
                uTile = uvg->GetUScl(t);
                vTile = uvg->GetVScl(t);
                // The importer's KTAR controller stores degree values
                // (shared with Wc3Bitmap's anim_WAngle) and the exporter
                // reads this channel as degrees too — match them.
                angDeg = uvg->GetWAng(t);
            }
        } else if (src.legacyMtl) {
            PB2Float(src.legacyMtl, L"anim_UOffset", t, uOff);
            PB2Float(src.legacyMtl, L"anim_VOffset", t, vOff);
            PB2Float(src.legacyMtl, L"anim_UTiling", t, uTile);
            PB2Float(src.legacyMtl, L"anim_VTiling", t, vTile);
            PB2Float(src.legacyMtl, L"anim_WAngle", t, angDeg);
        }

        // Max → MDX: U offset negates into KTAT.x (matches the exporter's
        // signA = -1), V passes through.
        const f32 tx = -uOff, ty = vOff;
        const f32 ang = angDeg * kDegToRad;
        const f32 c = std::cos(ang), si = std::sin(ang);
        const f32 a = uTile * c;
        const f32 b = -vTile * si;
        const f32 d = uTile * si;
        const f32 e = vTile * c;
        const f32 px = tx - 0.5f, py = ty - 0.5f;
        FrameState::TexAnimMatrix tam{};
        tam.textureAnimId = taId;
        tam.row0[0] = a;
        tam.row0[1] = b;
        tam.row0[2] = 0.0f;
        tam.row0[3] = a * px + b * py + 0.5f;
        tam.row1[0] = d;
        tam.row1[1] = e;
        tam.row1[2] = 0.0f;
        tam.row1[3] = d * px + e * py + 0.5f;
        state.texAnimMatrices.push_back(tam);
    }

    // Animated textures (IFL flipbooks): swap the layer's diffuse texture to
    // the frame the current Max time falls on, using the timing the importer
    // stored on the BitmapTex. Delivered through the same
    // FrameState::layerTextureIds channel the MDX adapter uses for KMTF.
    if (!iflAnims_.empty()) {
        std::vector<i32> sentIfl;
        for (auto& gs : geosets_) {
            Mtl* mtl = gs.node ? gs.node->GetMtl() : nullptr;
            if (!mtl)
                continue;
            auto it = mtlToId_.find(mtl);
            if (it == mtlToId_.end())
                continue;
            const i32 matId = it->second;
            if (std::find(sentIfl.begin(), sentIfl.end(), matId) != sentIfl.end())
                continue;
            sentIfl.push_back(matId);
            ForEachWc3SubMtl(mtl, [&](Mtl* sub, i32 layerIdx) {
                auto ia = iflAnims_.find(sub);
                if (ia == iflAnims_.end())
                    return;
                const IflAnim& anim = ia->second;
                const i64 n = (i64)anim.frameTexIds.size();
                if (n == 0)
                    return;
                const i64 interval = std::max<i64>(1, (i64)anim.intervalTicks);
                i64 rel = (i64)t - (i64)anim.startTime;
                if (rel < 0)
                    rel = 0;
                i64 idx = rel / interval;
                if (anim.endCondition == 2)
                    idx = std::min(idx, n - 1); // HOLD: clamp to last frame
                else
                    idx %= n; // LOOP (pingpong is approximated as loop)
                FrameState::LayerTextureIdState lts;
                lts.materialId = matId;
                lts.layerIndex = layerIdx;
                lts.slot = FrameState::LayerTexSlot::Diffuse;
                lts.textureId = anim.frameTexIds[(usize)idx];
                state.layerTextureIds.push_back(lts);
            });
        }
    }

    return state;
}

// ============================================================================
// RefreshMaterials — re-read material properties, detect changes
// ============================================================================

MaxSceneAdapter::MaterialRefreshResult MaxSceneAdapter::RefreshMaterials() {
    MaterialRefreshResult result;

    // Structural comparison of two snapshots — returns true if any tracked
    // field differs (equivalent to memberwise equality).
    auto snapshotsEqual = [](const MaterialSnapshot& a, const MaterialSnapshot& b) {
        return a.filterMode == b.filterMode && a.flags == b.flags &&
               a.priorityPlane == b.priorityPlane && a.sortOrder == b.sortOrder &&
               a.replaceableTexture == b.replaceableTexture && a.shaderId == b.shaderId &&
               a.texturePath == b.texturePath && a.normalTexPath == b.normalTexPath &&
               a.ormTexPath == b.ormTexPath && a.emissiveTexPath == b.emissiveTexPath &&
               a.teamColorTexPath == b.teamColorTexPath && a.hasUvAnim == b.hasUvAnim;
    };

    bool anyChanged = false;
    for (auto& mi : materials_) {
        if (anyChanged)
            break;
        ForEachWc3SubMtl(mi.mtl, [&](Mtl* sub, i32 layerIdx) {
            if (anyChanged)
                return;
            const i32 key = (sub == mi.mtl) ? mi.materialId : (mi.materialId * 1000 + layerIdx);
            MaterialSnapshot cur = SnapshotMaterial(sub);
            auto it = matSnapshots_.find(key);
            if (it == matSnapshots_.end() || !snapshotsEqual(it->second, cur))
                anyChanged = true;
        });
    }

    if (!anyChanged)
        return result;

    // Something changed — re-collect materials + textures from scratch.
    loadedTextures_.clear();
    texPathToId_.clear();
    texEntries_.clear();
    nextTexId_ = 0;
    CollectMaterials();
    UpdateMaterialSnapshots();

    result.materials = GetMaterials();
    result.textures = GetTextures();
    result.changed = true;
    return result;
}

// ============================================================================
// IModelSource::GetSequences() — Max doesn't have MDX sequences
// ============================================================================

std::vector<SequenceInfo> MaxSceneAdapter::GetSequences() const {
    return {};
}

// ============================================================================
// GetCameraPresets — collect camera objects from the Max scene
// ============================================================================

std::vector<CameraPreset> MaxSceneAdapter::GetCameraPresets() {
    std::vector<CameraPreset> presets;
    ForEachSceneNode([&](INode* node) {
        Object* obj = GetBaseObject(node);
        if (!obj || obj->SuperClassID() != CAMERA_CLASS_ID)
            return;

        // Max-space world transform; resolve the target from the node's
        // target link when present, otherwise project 100 units along -Z.
        Matrix3 tm = node->GetNodeTM(0);
        Point3 rawPos = tm.GetRow(3);
        Point3 rawTgt = rawPos + tm.GetRow(2) * -100.0f;
        if (INode* targNode = node->GetTarget())
            rawTgt = targNode->GetNodeTM(0).GetRow(3);

        // Lift into renderer-native space before deriving orbital parameters
        // so pitch/yaw match the camera's Z-up-around-target convention
        // regardless of WDX_DEFAULT_COORD_SPACE.
        Vector3f pos = MaxPointToDefault(rawPos);
        Vector3f tgt = MaxPointToDefault(rawTgt);

        Vector3f dir{pos.x - tgt.x, pos.y - tgt.y, pos.z - tgt.z};
        f32 dist = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (dist < 0.01f)
            dist = 100.0f;
        dir = {dir.x / dist, dir.y / dist, dir.z / dist};

        CameraPreset cp;
        // Max gives us a wide string; convert to UTF-8 once for the host UI.
        {
            const wchar_t* w = node->GetName();
            const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) {
                cp.name.assign(static_cast<usize>(n - 1), '\0');
                ::WideCharToMultiByte(CP_UTF8, 0, w, -1, cp.name.data(), n, nullptr, nullptr);
            }
        }
        cp.pitch = asinf(std::clamp(dir.z, -1.0f, 1.0f));
        cp.yaw = atan2f(dir.y, dir.x);
        cp.distance = dist;
        cp.target = tgt;
        cp.isLive = false;
        presets.push_back(cp);
    });
    return presets;
}

// ============================================================================
// ReadActiveViewportCamera — sample the active Max viewport as a camera pose
//
// Max's affine TM maps world → view, so its inverse is the view's own frame in
// world space. Row 2 there points back at the viewer (the SDK doc calls it
// "the view direction"), matching the −Z-is-forward convention GetCameraPresets
// already uses for camera nodes.
//
// Orthographic viewports — Top / Front / user iso — have no eye position to
// borrow, so they get emulated: the affine TM's origin is the world point at
// the centre of the viewport, and the eye is pushed back far enough that a
// perspective camera of kOrthoFovH spans the same world width the ortho view
// shows. The framing only drifts for geometry far off that centre plane, which
// is the price of previewing an ortho view through a perspective camera.
//
// FOV comes from GetVPWorldWidth rather than GetFOV: it is the viewport's
// world-space *width* at a given depth either way, so it needs no assumption
// about which screen axis Max measures its FOV across. GetFOV is the fallback
// for the degenerate readings.
// ============================================================================

// Qualified: this TU has no `namespace whiteout::flakes { … }` block, only a
// using-directive, so an unqualified definition would land in the global
// namespace and never match the declaration.
bool whiteout::flakes::ReadActiveViewportCamera(ViewportCameraPose& out) {
    Interface* ip = GetCOREInterface();
    if (!ip)
        return false;
    ViewExp& vp = ip->GetActiveViewExp();
    if (!vp.IsAlive())
        return false;

    Matrix3 affine;
    vp.GetAffineTM(affine);
    const Matrix3 viewToWorld = Inverse(affine);

    const Point3 origin = viewToWorld.GetRow(3);
    const Point3 back = Normalize(viewToWorld.GetRow(2)); // toward the viewer
    const Point3 upMax = Normalize(viewToWorld.GetRow(1));

    // Nominal horizontal FOV for orthographic viewports, which have none of
    // their own. Matches Max's own default user-perspective field of view.
    constexpr f32 kOrthoFovH = 0.7853982f; // 45°

    Point3 eyeMax, tgtMax;
    f32 sampleDist = 0.0f;
    if (vp.IsPerspView()) {
        sampleDist = vp.GetFocalDist();
        if (!(sampleDist > 0.01f))
            sampleDist = 1000.0f;
        eyeMax = origin;
        tgtMax = origin - back * sampleDist;

        const f32 worldWidth = vp.GetVPWorldWidth(tgtMax);
        out.fovHorizontal = (worldWidth > 0.01f)
                                ? 2.0f * std::atan((worldWidth * 0.5f) / sampleDist)
                                : vp.GetFOV();
    } else {
        out.fovHorizontal = kOrthoFovH;
        f32 worldWidth = vp.GetVPWorldWidth(origin);
        if (!(worldWidth > 0.01f))
            worldWidth = 1000.0f;
        sampleDist = (worldWidth * 0.5f) / std::tan(kOrthoFovH * 0.5f);
        tgtMax = origin;
        eyeMax = origin + back * sampleDist;
    }
    if (!(out.fovHorizontal > 0.01f) || out.fovHorizontal >= std::numbers::pi_v<f32>)
        out.fovHorizontal = kOrthoFovH;

    out.position = MaxPointToDefault(eyeMax);
    out.target = MaxPointToDefault(tgtMax);

    // Roll. Camera::SetDirectPose rebuilds `up` from the look direction and
    // world Z, then rotates it by `roll` — so recover the angle between that
    // zero-roll up and the viewport's actual one, mirroring the same basis
    // construction Camera::ComputeUpFromLookDirection uses. This is what keeps
    // a Top view (whose look direction is world Z, where the basis degenerates
    // and falls back to +X) from arriving rotated 90°.
    out.roll = 0.0f;
    Vector3f fwd{out.target.x - out.position.x, out.target.y - out.position.y,
                 out.target.z - out.position.z};
    if (fwd.length() > 1e-4f) {
        fwd = fwd.normalized();
        const Vector3f worldUp{0.0f, 0.0f, 1.0f};
        Vector3f right = cross(fwd, worldUp);
        if (right.length_squared() < 1e-6f)
            right = cross(fwd, Vector3f{1.0f, 0.0f, 0.0f});
        right = right.normalized();
        const Vector3f upBase = cross(right, fwd).normalized();
        const Vector3f up = MaxDirToDefault(upMax);
        out.roll = std::atan2(up.dot(right), up.dot(upBase));
    }
    return true;
}
