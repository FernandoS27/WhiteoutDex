// MDLXExporter — Wc3Material extractor implementation
#include "wc3_material_extractor.h"
#include <wdx_text.h>
#include "../mdx_class_ids.h"
#include "../material_fix_settings.h"

#include <wdx_foreign_material.h>
#include <wdx_replaceable_ids.h>

#include <scene/paramblock_reader.h>
#include <animation/global_sequence_helper.h>
#include <max.h>
#include <maxversion.h>
#include <stdmat.h>
#include <bitmap.h>
#include <inode.h>
#include <istdplug.h>
#include <control.h>
#include <iparamb2.h>
#include <IFileResolutionManager.h>
#include <modstack.h>
#include <tchar.h>
#include <windows.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <utility>
#include <fstream>
#include <cctype>

namespace mdx_extract {

namespace {

std::string wstrToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

// ── NeoDex compatibility: try alternative parameter names ──────────
// NeoDex "Warcraft 3" material uses different param names than WhiteoutDex.
// These helpers try the primary (WhiteoutDex) name first, then fall back
// to the NeoDex alternative. Both materials extend Standard, so the
// ParamBlockReader functions work the same way on both.
//
// Note: uses full namespace core::ParamBlockReader because the local
// "using PBR = ..." alias is only in scope inside individual functions.
static bool readBoolFB(ReferenceTarget* ref, const wchar_t* primary,
                       const wchar_t* fallback, TimeValue t, BOOL& val) {
    if (core::ParamBlockReader::readBoolByName(ref, primary, t, val)) return true;
    if (fallback && core::ParamBlockReader::readBoolByName(ref, fallback, t, val)) return true;
    return false;
}
static bool readIntFB(ReferenceTarget* ref, const wchar_t* primary,
                      const wchar_t* fallback, TimeValue t, int& val) {
    if (core::ParamBlockReader::readIntByName(ref, primary, t, val)) return true;
    if (fallback && core::ParamBlockReader::readIntByName(ref, fallback, t, val)) return true;
    return false;
}
static bool readFloatFB(ReferenceTarget* ref, const wchar_t* primary,
                        const wchar_t* fallback, TimeValue t, float& val) {
    if (core::ParamBlockReader::readFloatByName(ref, primary, t, val)) return true;
    if (fallback && core::ParamBlockReader::readFloatByName(ref, fallback, t, val)) return true;
    return false;
}
static bool readStringFB(ReferenceTarget* ref, const wchar_t* primary,
                         const wchar_t* fallback, TimeValue t, std::wstring& val) {
    if (core::ParamBlockReader::readStringByName(ref, primary, t, val)) return true;
    if (fallback && core::ParamBlockReader::readStringByName(ref, fallback, t, val)) return true;
    return false;
}
// NeoDex fallback for texmap lookup
static bool readTexmapFB(ReferenceTarget* ref, const wchar_t* primary,
                         const wchar_t* fallback, Texmap*& val) {
    if (core::ParamBlockReader::readTexmapByName(ref, primary, val) && val) return true;
    if (fallback && core::ParamBlockReader::readTexmapByName(ref, fallback, val) && val) return true;
    return false;
}

// Return just the filename (no directory) from the bitmap's disk path.
// The WC3 texture path prefix (e.g. "Textures\", "war3mapImported\") is
// stored separately on the Wc3Material and must be prepended by the caller
// via getMaterialPrefix().
std::string extractBitmapFileName(Texmap* tex) {
    auto stripPath = [](const std::string& p) -> std::string {
        if (p.empty()) return p;
        // Find last \ or /
        size_t lastSep = std::string::npos;
        for (size_t i = p.size(); i > 0; --i) {
            char c = p[i - 1];
            if (c == '\\' || c == '/') { lastSep = i - 1; break; }
        }
        return (lastSep != std::string::npos) ? p.substr(lastSep + 1) : p;
    };

    if (!tex) return {};
    // Standard Bitmaptexture
    if (tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
        auto* bmt = static_cast<BitmapTex*>(tex);
        return stripPath(wdx::text::wideToMdx(bmt->GetMapName()));
    }
    // Wc3Bitmap — find BitmapTex delegate in references
    if (tex->ClassID() == mdx_ids::WC3_BITMAP) {
        for (int i = 0; i < tex->NumRefs(); i++) {
            ReferenceTarget* ref = tex->GetReference(i);
            if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                auto* bmt = static_cast<BitmapTex*>(ref);
                return stripPath(wdx::text::wideToMdx(bmt->GetMapName()));
            }
        }
    }
    return {};
}

// Read a texture path prefix from the Wc3Material. The prefix is per-slot
// and was stored by the importer (e.g. diffusePrefix = "Textures\").
std::string readMaterialPrefix(ReferenceTarget* mtlRef, const wchar_t* paramName) {
    if (!mtlRef) return {};
    using PBR = core::ParamBlockReader;
    std::wstring prefix;
    if (PBR::readStringByName(mtlRef, paramName, 0, prefix) && !prefix.empty())
        return wdx::text::wideToMdx(prefix.c_str());
    return {};
}

// Combine a prefix and a filename into an MDX texture path.
// If the filename already contains a path separator, return it unchanged
// (safety net matching Wc3Material.ms buildTexturePath).
std::string buildTexturePath(const std::string& prefix, const std::string& fileName) {
    if (fileName.empty()) return {};
    if (fileName.find('\\') != std::string::npos ||
        fileName.find('/')  != std::string::npos)
    {
        return fileName;  // already full
    }
    return prefix + fileName;
}

// Kept for backward-compat — callers should migrate to extractBitmapFileName +
// buildTexturePath + readMaterialPrefix. This now returns JUST the filename
// (no absolute path), which is safer even for callers that don't know about
// the material-level prefix.
std::string extractBitmapPath(Texmap* tex) {
    return extractBitmapFileName(tex);
}

// Full on-disk path of the BitmapTex (or BitmapTex delegate of a Wc3Bitmap).
// Used by the exporter to feed texture conversion at write-time.
std::string extractBitmapDiskPath(Texmap* tex) {
    if (!tex) return {};
    if (tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
        auto* bmt = static_cast<BitmapTex*>(tex);
        const MCHAR* mp = bmt->GetMapName();
        return mp ? wstrToUtf8(mp) : std::string{};
    }
    if (tex->ClassID() == mdx_ids::WC3_BITMAP) {
        for (int i = 0; i < tex->NumRefs(); i++) {
            ReferenceTarget* ref = tex->GetReference(i);
            if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                auto* bmt = static_cast<BitmapTex*>(ref);
                const MCHAR* mp = bmt->GetMapName();
                return mp ? wstrToUtf8(mp) : std::string{};
            }
        }
    }
    return {};
}

// ── Texture source resolution ──────────────────────────────────────
// A bitmap whose file is gone cannot be converted. NeoDex scenes are the
// common case: the NeoDex importer decodes every BLP to
// %TEMP%\BLP_Textures\<name>.tga and empties that folder on the next import,
// or, when it found no texture at all, points the bitmap at a <name>.tga next
// to the model that never existed. The texture itself usually still sits near
// the scene, under its own name with another extension (the original .blp).

namespace fs = std::filesystem;

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring w(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

std::wstring lowerWide(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

// Formats texture conversion can decode, in the order a lookup prefers them.
const wchar_t* const kTextureExts[] = {
    L".blp", L".dds", L".tga", L".png", L".bmp", L".jpg", L".jpeg"
};

bool isTextureExt(const std::wstring& lowerExt) {
    for (const wchar_t* e : kTextureExts)
        if (lowerExt == e) return true;
    return false;
}

// Texture files under the scene folder by lower-case name without extension.
// Built on the first miss of an export and reused until the scene folder
// changes; extractMaterials() clears it at the start of every export.
struct SceneTextureIndex {
    fs::path root;
    bool built = false;
    std::unordered_map<std::wstring, std::vector<fs::path>> byStem;
};
SceneTextureIndex g_sceneTextures;

void buildSceneTextureIndex(const fs::path& root) {
    g_sceneTextures = {};
    g_sceneTextures.root = root;
    g_sceneTextures.built = true;
    // Bounded: a scene saved at a drive root or in Downloads must not stall
    // the export. Four folder levels cover "Textures\", "units\x\y\" and the
    // like; the entry cap stops runaway trees.
    constexpr int    kMaxDepth   = 4;
    constexpr size_t kMaxEntries = 50000;
    std::error_code ec;
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    size_t visited = 0;
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (++visited > kMaxEntries) break;
        std::error_code fec;
        if (it->is_directory(fec)) {
            if (it.depth() + 1 >= kMaxDepth) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(fec)) continue;
        const fs::path& p = it->path();
        if (!isTextureExt(lowerWide(p.extension().wstring()))) continue;
        g_sceneTextures.byStem[lowerWide(p.stem().wstring())].push_back(p);
    }
}

// The file a missing texture source most likely is, or `diskPath` unchanged.
// Looks, in order, beside the missing file, in the scene folder under the MDX
// sub path and directly, then asks Max's own asset resolution (project
// folder, scene folder, user map paths — IFileResolutionManager), and last
// searches the scene folder tree. Each place is tried with the original
// extension first, then every other format conversion can read.
std::string resolveTextureSource(const std::string& diskPath,
                                 const std::string& mdxPath) {
    if (diskPath.empty()) return diskPath;
    const fs::path given(utf8ToWide(diskPath));
    std::error_code ec;
    if (fs::exists(given, ec)) return diskPath;

    const std::wstring stem = given.stem().wstring();
    if (stem.empty()) return diskPath;
    std::vector<std::wstring> exts;
    const std::wstring givenExt = lowerWide(given.extension().wstring());
    if (isTextureExt(givenExt)) exts.push_back(givenExt);
    for (const wchar_t* e : kTextureExts)
        if (givenExt != e) exts.push_back(e);

    fs::path sceneDir;
    if (Interface* ip = GetCOREInterface()) {
        const MSTR scene = ip->GetCurFilePath();
        if (scene.Length() > 0) sceneDir = fs::path(scene.data()).parent_path();
    }

    std::vector<fs::path> dirs;
    if (given.has_parent_path()) dirs.push_back(given.parent_path());
    if (!sceneDir.empty()) {
        std::wstring rel = utf8ToWide(mdxPath);
        std::replace(rel.begin(), rel.end(), L'\\', L'/');
        const fs::path relDir = fs::path(rel).parent_path();
        if (!relDir.empty()) dirs.push_back(sceneDir / relDir);
        dirs.push_back(sceneDir);
    }
    for (const fs::path& dir : dirs)
        for (const std::wstring& ext : exts) {
            const fs::path p = dir / (stem + ext);
            if (fs::is_regular_file(p, ec)) return wstrToUtf8(p.wstring().c_str());
        }

    if (IFileResolutionManager* frm = IFileResolutionManager::GetInstance())
        for (const std::wstring& ext : exts) {
            const MSTR found = frm->GetFullFilePath((stem + ext).c_str(),
                                   MaxSDK::AssetManagement::kBitmapAsset);
            if (found.Length() > 0 && fs::is_regular_file(fs::path(found.data()), ec))
                return wstrToUtf8(found.data());
        }

    if (!sceneDir.empty()) {
        if (!g_sceneTextures.built || g_sceneTextures.root != sceneDir)
            buildSceneTextureIndex(sceneDir);
        auto hit = g_sceneTextures.byStem.find(lowerWide(stem));
        if (hit != g_sceneTextures.byStem.end())
            for (const std::wstring& ext : exts)
                for (const fs::path& p : hit->second)
                    if (lowerWide(p.extension().wstring()) == ext)
                        return wstrToUtf8(p.wstring().c_str());
    }
    return diskPath;
}

} // end anonymous namespace — findOrAddTexture is exported via header

int32_t findOrAddTexture(ir::IRModel& model, const std::string& path,
                         int32_t replaceableId, bool wrapU, bool wrapV,
                         const std::string& sourceDiskPathIn)
{
    const std::string sourceDiskPath = resolveTextureSource(sourceDiskPathIn, path);
    // Look for an existing match
    for (size_t i = 0; i < model.textures.size(); i++) {
        auto& t = model.textures[i];
        if (t.filePath == path && t.replaceableId == replaceableId &&
            t.wrapU == wrapU && t.wrapV == wrapV) {
            // Populate source disk path on the first reuse if it wasn't
            // captured at the original insertion point.
            if (t.sourceDiskPath.empty() && !sourceDiskPath.empty())
                t.sourceDiskPath = sourceDiskPath;
            return static_cast<int32_t>(i);
        }
    }
    ir::Texture tex;
    tex.filePath = path;
    tex.replaceableId = replaceableId;
    tex.wrapU = wrapU;
    tex.wrapV = wrapV;
    tex.sourceDiskPath = sourceDiskPath;
    model.textures.push_back(std::move(tex));
    return static_cast<int32_t>(model.textures.size() - 1);
}

namespace {  // reopen anon namespace for remaining internal helpers

// Extract properties from a Wc3Bitmap texture plugin, with Standard-BitmapTex
// fallback for wrapU/wrapV so that materials whose texmap is a plain BitmapTex
// (e.g. new Wc3Material-based imports) still get correct wrap flags instead of
// silently defaulting to false.
struct BitmapProperties {
    int replaceableId = 0;
    bool wrapU = false;
    bool wrapV = false;
    bool sphereEnvMap = false;
    std::string prefixPath;
};

// Read wrapU/wrapV from a Standard BitmapTex's UVGen. The flag bits are
// defined in imtl.h (Max SDK):
//   U_WRAP   = (1<<0) = 0x1
//   V_WRAP   = (1<<1) = 0x2
//   U_MIRROR = (1<<2) = 0x4
//   V_MIRROR = (1<<3) = 0x8
// We use the named macros if available so we pick up any SDK change.
#ifndef U_WRAP
  #define U_WRAP (1<<0)
#endif
#ifndef V_WRAP
  #define V_WRAP (1<<1)
#endif
static void readWrapFromBitmapTex(BitmapTex* bmt, bool& wrapU, bool& wrapV) {
    if (!bmt) return;
    UVGen* uv = bmt->GetTheUVGen();
    if (!uv) return;
    StdUVGen* stdUv = dynamic_cast<StdUVGen*>(uv);
    if (!stdUv) {
        // Not a StdUVGen — fall back to "wrap" (most common case)
        wrapU = true;
        wrapV = true;
        return;
    }
    int flags = stdUv->GetTextureTiling();
    wrapU = (flags & U_WRAP) != 0;
    wrapV = (flags & V_WRAP) != 0;
}

BitmapProperties extractBitmapProperties(Texmap* tex) {
    BitmapProperties props;
    if (!tex) return props;

    // Path A: Wc3Bitmap — read properties directly from its ParamBlock
    if (tex->ClassID() == mdx_ids::WC3_BITMAP) {
        auto* ref = dynamic_cast<ReferenceTarget*>(tex);
        if (!ref) return props;

        using PBR = core::ParamBlockReader;
        TimeValue t = 0;

        // Wc3Bitmap's dropdown holds a list position (wdx_replaceable_ids.h);
        // NeoDex's "retexture" list is 0 / 1 / 2 itself, so position - 1.
        int replId = 1; // 1 = Not Used
        if (PBR::readIntByName(ref, L"replaceableId", t, replId) && replId > 0) {
            props.replaceableId = wdx::replaceable::IdFromDropdown(replId);
        } else {
            replId = 0;
            PBR::readIntByName(ref, L"retexture", t, replId);
            props.replaceableId = std::max(0, replId - 1);
        }

        BOOL flag = FALSE;
        const bool hasWrapU = PBR::readBoolByName(ref, L"wrapU", t, flag);
        if (hasWrapU && flag) props.wrapU = true;
        flag = FALSE;
        const bool hasWrapV = PBR::readBoolByName(ref, L"wrapV", t, flag);
        if (hasWrapV && flag) props.wrapV = true;
        flag = FALSE;
        if (PBR::readBoolByName(ref, L"sphereEnvMap", t, flag) && flag) props.sphereEnvMap = true;

        std::wstring prefix;
        if (PBR::readStringByName(ref, L"prefixPath", t, prefix) && !prefix.empty())
            props.prefixPath = wdx::text::wideToMdx(prefix.c_str());

        // The delegate's tiling is only a fallback for a Wc3Bitmap without
        // the wrap params. When they exist they are the truth: Wc3Material's
        // filter-mode handler switches tiling ON for the bitmap it uses as the
        // opacity map of an additive layer (the diffuse's own delegate), and
        // reading that back turned a clamped Team Glow texture into flags 3.
        if (!hasWrapU && !hasWrapV) {
            for (int i = 0; i < tex->NumRefs(); i++) {
                ReferenceTarget* delegate = tex->GetReference(i);
                if (delegate && delegate->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                    readWrapFromBitmapTex(static_cast<BitmapTex*>(delegate),
                                          props.wrapU, props.wrapV);
                    break;
                }
            }
        }
        return props;
    }

    // Path B: Standard BitmapTex — no replaceableId/sphereEnvMap/prefixPath,
    // but we can still read wrap flags from its UVGen so imports that use
    // plain BitmapTex (new Wc3Material-based workflow) get correct TEXS flags.
    if (tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
        readWrapFromBitmapTex(static_cast<BitmapTex*>(tex),
                              props.wrapU, props.wrapV);
        return props;
    }

    return props;
}

// ── UV animation extraction (TXAN) ──────────────────────────────────
// WC3 Texture Animations are animated UV Translation / Rotation / Scaling
// tracks. They live on the TEXTURE — the Wc3Bitmap plugin's ParamBlock2
// params `anim_UOffset`, `anim_VOffset`, `anim_WOffset`, `anim_WAngle`,
// `anim_UTiling`, `anim_VTiling` (shared with the delegate BitmapTex's
// own StdUVGen coords, so viewport and export always agree).
//
// Per-channel controller resolution order (see resolveUVAnimControllers):
//   1. Wc3Bitmap anim_* params on the diffuse texmap        (current scheme)
//   2. the diffuse BitmapTex's StdUVGen tracks              (plain BitmapTex /
//      legacy scenes whose material params were dropped on load)
//   3. Wc3Material-level anim_* params                      (legacy scenes
//      saved with the old plugin definition still loaded)
//   4. NeoDex material params (Wc3_U_Offset, ...)           (NeoDex scenes)

// Find a named parameter in any IParamBlock2 on the target.
static bool findAnimParam(ReferenceTarget* ref, const wchar_t* name,
                          IParamBlock2*& outPB, ParamID& outPID)
{
    if (!ref) return false;
    auto search = [&](IParamBlock2* pb) {
        ParamBlockDesc2* desc = pb ? pb->GetDesc() : nullptr;
        if (!desc) return false;
        for (int j = 0; j < desc->Count(); j++) {
            ParamID pid = desc->IndextoID(j);
            const ParamDef& pd = desc->GetParamDef(pid);
            if (pd.int_name && _wcsicmp(pd.int_name, name) == 0) {
                outPB = pb;
                outPID = pid;
                return true;
            }
        }
        return false;
    };
    for (int i = 0; i < ref->NumParamBlocks(); i++)
        if (search(ref->GetParamBlock(i))) return true;
    // Some classes keep their IParamBlock2 as a plain reference without
    // exposing it through GetParamBlock — StdUVGen, whose U_Offset / V_Offset
    // / W_Angle / tiling tracks a bitmap's texture animation lives on, among
    // them. Without this a UV animation keyed on the bitmap alone was lost.
    for (int i = 0; i < ref->NumRefs(); i++)
        if (search(dynamic_cast<IParamBlock2*>(ref->GetReference(i)))) return true;
    return false;
}

// Get the controller assigned to a named animatable parameter.
// IParamBlock2 exposes animated parameters as sub-anims; GetAnimNum maps
// ParamID → sub-anim index, SubAnim returns the Animatable (a Control*).
static Control* getParamController(ReferenceTarget* ref, const wchar_t* name) {
    IParamBlock2* pb = nullptr;
    ParamID pid = 0;
    if (!findAnimParam(ref, name, pb, pid)) return nullptr;
    int animIdx = pb->GetAnimNum(pid, 0);
    if (animIdx < 0 || animIdx >= pb->NumSubs()) return nullptr;
    Animatable* anim = pb->SubAnim(animIdx);
    if (!anim) return nullptr;
    return GetControlInterface(anim);
}

// Unwrap a Texmap to its native BitmapTex: the texmap itself when it is one,
// the BitmapTex delegate when it's a Wc3Bitmap wrapper, else nullptr.
static BitmapTex* unwrapBitmapTex(Texmap* tex) {
    if (!tex) return nullptr;
    if (tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0))
        return static_cast<BitmapTex*>(tex);
    if (tex->ClassID() == mdx_ids::WC3_BITMAP) {
        for (int i = 0; i < tex->NumRefs(); i++) {
            ReferenceTarget* ref = tex->GetReference(i);
            if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0))
                return static_cast<BitmapTex*>(ref);
        }
    }
    return nullptr;
}

// Controller on a StdUVGen track, looked up by PB2 internal name with a
// sub-anim index fallback. Track indices: 0=U_Offset, 1=V_Offset,
// 2=U_Tiling, 3=V_Tiling, 4=U_Angle, 5=V_Angle, 6=W_Angle.
// StdUVGen keeps these in an old-style (non-PB2) parameter block that is its
// single sub-anim, so the tracks are one level down: uvGen->SubAnim(0) is the
// block, and the block's sub-anim i is param i's controller (null while the
// param is not animated). MAXScript flattens that level, which is why
// `coords[1]` there is U_Offset. Measured in Max 2027: NumSubs() == 1.
static Control* getUVGenController(StdUVGen* uvGen, const wchar_t* name,
                                   int subAnimIdx) {
    if (!uvGen) return nullptr;
    if (Control* c = getParamController(uvGen, name)) return c;
    if (subAnimIdx < 0) return nullptr;
    Animatable* holder = uvGen;
    if (uvGen->NumSubs() == 1 && uvGen->SubAnim(0) &&
        uvGen->SubAnim(0)->NumSubs() > subAnimIdx)
        holder = uvGen->SubAnim(0);
    if (subAnimIdx < holder->NumSubs())
        if (Animatable* anim = holder->SubAnim(subAnimIdx))
            return GetControlInterface(anim);
    return nullptr;
}

// Resolved per-channel UV-animation controllers for one material layer.
struct UVAnimControllers {
    Control* uOffset = nullptr;
    Control* vOffset = nullptr;
    Control* wOffset = nullptr;   // param-only channel (StdUVGen has no W offset)
    Control* wAngle  = nullptr;
    Control* uTiling = nullptr;
    Control* vTiling = nullptr;
    bool any() const {
        return uOffset || vOffset || wOffset || wAngle || uTiling || vTiling;
    }
};

// Resolve the UV-animation controllers for a layer. The authoritative home
// is the diffuse texmap (Wc3Bitmap anim_* params / BitmapTex StdUVGen);
// material-level params are read only as a legacy / NeoDex fallback.
static UVAnimControllers resolveUVAnimControllers(ReferenceTarget* mtlRef,
                                                  Texmap* diffuseTex) {
    UVAnimControllers uv;

    ReferenceTarget* bmpRef = nullptr;
    if (diffuseTex && diffuseTex->ClassID() == mdx_ids::WC3_BITMAP)
        bmpRef = dynamic_cast<ReferenceTarget*>(diffuseTex);

    StdUVGen* uvGen = nullptr;
    if (BitmapTex* bmt = unwrapBitmapTex(diffuseTex))
        uvGen = dynamic_cast<StdUVGen*>(bmt->GetTheUVGen());

    auto pick = [&](const wchar_t* animName, const wchar_t* uvGenName,
                    int subAnimIdx, const wchar_t* neoDexName) -> Control* {
        if (bmpRef)
            if (Control* c = getParamController(bmpRef, animName)) return c;
        if (uvGenName && uvGen)
            if (Control* c = getUVGenController(uvGen, uvGenName, subAnimIdx))
                return c;
        if (mtlRef)
            if (Control* c = getParamController(mtlRef, animName)) return c;
        if (mtlRef && neoDexName)
            if (Control* c = getParamController(mtlRef, neoDexName)) return c;
        return nullptr;
    };

    uv.uOffset = pick(L"anim_UOffset", L"U_Offset", 0, L"Wc3_U_Offset");
    uv.vOffset = pick(L"anim_VOffset", L"V_Offset", 1, L"Wc3_V_Offset");
    uv.wOffset = pick(L"anim_WOffset", nullptr,    -1, L"Wc3_W_Offset");
    uv.wAngle  = pick(L"anim_WAngle",  L"W_Angle",  6, L"Wc3_W_Angle");
    uv.uTiling = pick(L"anim_UTiling", L"U_Tiling", 2, L"Wc3_U_Tiling");
    uv.vTiling = pick(L"anim_VTiling", L"V_Tiling", 3, L"Wc3_V_Tiling");
    return uv;
}

// Evaluate a float controller at a specific time.
static float evalFloat(Control* ctrl, TimeValue t, float defaultVal = 0.0f) {
    if (!ctrl) return defaultVal;
    float val = defaultVal;
    Interval valid = FOREVER;
    ctrl->GetValue(t, &val, valid);
    return val;
}

// Collect all key times from any keyframe controller.
static std::vector<TimeValue> collectKeyTimes(Control* ctrl) {
    std::vector<TimeValue> times;
    if (!ctrl) return times;
    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (ikc) {
        int n = ikc->GetNumKeys();
        times.reserve(n);
        ULONG cidA = ctrl->ClassID().PartA();
        for (int i = 0; i < n; i++) {
            TimeValue t = 0;
            if (cidA == LININTERP_FLOAT_CLASS_ID) {
                ILinFloatKey k; ikc->GetKey(i, &k); t = k.time;
            } else if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) {
                IBezFloatKey k; ikc->GetKey(i, &k); t = k.time;
            } else if (cidA == TCBINTERP_FLOAT_CLASS_ID) {
                ITCBFloatKey k; ikc->GetKey(i, &k); t = k.time;
            } else {
                IBezFloatKey k; ikc->GetKey(i, &k); t = k.time;
            }
            times.push_back(t);
        }
    } else {
        int n = ctrl->NumKeys();
        times.reserve(n);
        for (int i = 0; i < n; i++) times.push_back(ctrl->GetKeyTime(i));
    }
    return times;
}

// Map a controller's ClassID to an interpolation type.
static ir::InterpolationType detectInterpFromController(Control* ctrl) {
    if (!ctrl) return ir::InterpolationType::None;
    ULONG cidA = ctrl->ClassID().PartA();
    if (cidA == LININTERP_FLOAT_CLASS_ID)    return ir::InterpolationType::Linear;
    if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) return ir::InterpolationType::Bezier;
    if (cidA == TCBINTERP_FLOAT_CLASS_ID)    return ir::InterpolationType::Hermite;
    return ir::InterpolationType::None;
}

// A bezier_float controller whose keys all hold with STEP out-tangents is a
// DontInterp track: the importer stores MDX DontInterp alpha tracks exactly
// this way (BEZKEY_STEP on both sides of every key). Detecting it here lets
// the exporter round-trip DontInterp instead of writing a Bezier track whose
// reconstructed tangents overshoot (negative alpha) and crossfade a form
// switch that should be instant.
static bool isStepBezierController(Control* ctrl) {
    if (!ctrl || ctrl->ClassID().PartA() != HYBRIDINTERP_FLOAT_CLASS_ID)
        return false;
    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (!ikc) return false;
    int n = ikc->GetNumKeys();
    if (n == 0) return false;
    for (int i = 0; i < n; i++) {
        IBezFloatKey k;
        ikc->GetKey(i, &k);
        if (GetOutTanType(k.flags) != BEZKEY_STEP)
            return false;
    }
    return true;
}

// Read a float controller's keys into parallel vectors: time, value, in-tangent,
// out-tangent. hasTangents is set to true iff the controller is Bezier (MDX
// Bezier control points) or TCB (MDX Hermite tangents). For Linear/other,
// tangents are zero-filled.
static void readFloatKeys(Control* ctrl,
                          std::vector<TimeValue>& times,
                          std::vector<float>& values,
                          std::vector<float>& inTans,
                          std::vector<float>& outTans,
                          bool& hasTangents)
{
    hasTangents = false;
    if (!ctrl) return;
    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (!ikc) return;
    int n = ikc->GetNumKeys();
    if (n == 0) return;

    ULONG cidA = ctrl->ClassID().PartA();
    times.reserve(n); values.reserve(n);
    inTans.reserve(n); outTans.reserve(n);

    const bool tcb = (cidA == TCBINTERP_FLOAT_CLASS_ID);
    if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID || tcb) {
        // NeoDex-compatible Bezier tangent extraction (Wc3Animation.ms FloatKeys +
        // ProcessBezier). Max's IBezFloatKey.intan/.outtan are internal tangent
        // SLOPES (value/tick), NOT the control-point values that MDX expects.
        // NeoDex solves this by sampling the controller at 1/3 and 2/3 points
        // between each pair of keys, then applying BezierInTan/BezierOutTan
        // formulas to reconstruct the cubic Bezier control points.
        //
        // A TCB segment without ease is a cubic in time as well, so the same
        // control points hold it exactly; MDX Hermite wants them as
        // derivatives: out = 3*(P1 - P0), in = 3*(P3 - P2).
        hasTangents = true;

        // ── Pass 1: read key times and evaluate values ──
        struct TempKey { TimeValue time; float value; float sampleIn; float sampleOut; };
        std::vector<TempKey> tk(n);
        for (int i = 0; i < n; i++) {
            tk[i].time = ctrl->GetKeyTime(i);
            Interval valid = FOREVER;
            ctrl->GetValue(tk[i].time, &tk[i].value, valid);
        }

        // ── Pass 2: sample at 1/3 and 2/3 points between adjacent keys ──
        // NeoDex FloatKeys (line 2521-2528):
        //   in1  = c.value at (prevKey.time + 2/3 * (curKey.time - prevKey.time))
        //   out1 = c.value at (curKey.time  + 1/3 * (nextKey.time - curKey.time))
        for (int i = 0; i < n; i++) {
            if (i > 0) {
                TimeValue t23 = tk[i-1].time +
                    (TimeValue)(2.0 * (double)(tk[i].time - tk[i-1].time) / 3.0);
                Interval v = FOREVER;
                ctrl->GetValue(t23, &tk[i].sampleIn, v);
            } else {
                tk[i].sampleIn = tk[i].value;
            }

            if (i < n - 1) {
                TimeValue t13 = tk[i].time +
                    (TimeValue)((double)(tk[i+1].time - tk[i].time) / 3.0);
                Interval v = FOREVER;
                ctrl->GetValue(t13, &tk[i].sampleOut, v);
            } else {
                tk[i].sampleOut = tk[i].value;
            }
        }

        // ── Pass 3: compute MDX Bezier control points ──
        // NeoDex ProcessBezier (line 283-284):
        //   BezierOutTan(pe, pd, a, d) = 3*pe + (2*d - 5*a - 9*pd) / 6
        //   BezierInTan (pe, pd, a, d) = 3*pd + (2*a - 9*pe - 5*d) / 6
        //
        // For segment key[i] → key[i+1]:
        //   pe = key[i].sampleOut    (sampled at 1/3 from i to i+1)
        //   pd = key[i+1].sampleIn   (sampled at 2/3 from i to i+1)
        //   a  = key[i].value
        //   d  = key[i+1].value
        for (int i = 0; i < n; i++) {
            float val = tk[i].value;
            float inTan, outTan;

            if (i > 0) {
                float pe = tk[i-1].sampleOut;
                float pd = tk[i].sampleIn;
                float a  = tk[i-1].value;
                float d  = tk[i].value;
                inTan = 3.0f*pd + (2.0f*a - 9.0f*pe - 5.0f*d) / 6.0f;
            } else {
                inTan = val;
            }

            if (i < n - 1) {
                float pe = tk[i].sampleOut;
                float pd = tk[i+1].sampleIn;
                float a  = tk[i].value;
                float d  = tk[i+1].value;
                outTan = 3.0f*pe + (2.0f*d - 5.0f*a - 9.0f*pd) / 6.0f;
            } else {
                outTan = val;
            }

            if (tcb) {
                inTan  = 3.0f * (val - inTan);
                outTan = 3.0f * (outTan - val);
            } else {
                // Snap tangents near value (NeoDex ProcessBezier lines 293-296)
                if (fabsf(inTan - val) < 0.01f) inTan = val;
                if (fabsf(outTan - val) < 0.01f) outTan = val;
            }

            times.push_back(tk[i].time);
            values.push_back(val);
            inTans.push_back(inTan);
            outTans.push_back(outTan);
        }
    } else if (cidA == LININTERP_FLOAT_CLASS_ID) {
        // Linear: no tangents used
        for (int i = 0; i < n; i++) {
            ILinFloatKey k; ikc->GetKey(i, &k);
            times.push_back(k.time);
            values.push_back(k.val);
            inTans.push_back(0.0f);
            outTans.push_back(0.0f);
        }
    } else {
        // Unknown/other — fall back to Bezier key read + GetValue for value
        for (int i = 0; i < n; i++) {
            IBezFloatKey k; ikc->GetKey(i, &k);
            TimeValue t = k.time;
            float v = 0.0f;
            Interval iv = FOREVER;
            ctrl->GetValue(t, &v, iv);
            times.push_back(t);
            values.push_back(v);
            inTans.push_back(0.0f);
            outTans.push_back(0.0f);
        }
    }
}

// Extract a Vec3 track from two float controllers (U, V). The third
// component gets `defaultW`. negateFactor is applied to the U component
// (use -1.0f to match importer convention where MDX X = -U_Offset).
static int32_t extractVec3FromTwoCtrls(Control* ctrlU, Control* ctrlV,
                                       float defaultU, float defaultV,
                                       float defaultW,
                                       ir::InterpolationType interp,
                                       ir::IRModel& model,
                                       float negateFactor = 1.0f)
{
    if (!ctrlU && !ctrlV) return -1;

    // Read per-controller keys with tangents
    std::vector<TimeValue> kTimesU, kTimesV;
    std::vector<float> kValsU, kValsV, kTiU, kToU, kTiV, kToV;
    bool tanU = false, tanV = false;
    readFloatKeys(ctrlU, kTimesU, kValsU, kTiU, kToU, tanU);
    readFloatKeys(ctrlV, kTimesV, kValsV, kTiV, kToV, tanV);

    if (kTimesU.empty() && kTimesV.empty()) return -1;

    // Union of key times
    std::vector<TimeValue> times;
    times.reserve(kTimesU.size() + kTimesV.size());
    for (auto t : kTimesU) times.push_back(t);
    for (auto t : kTimesV) times.push_back(t);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    // Tangent lookup by time
    std::unordered_map<TimeValue, size_t> tanIdxU, tanIdxV;
    for (size_t i = 0; i < kTimesU.size(); i++) tanIdxU[kTimesU[i]] = i;
    for (size_t i = 0; i < kTimesV.size(); i++) tanIdxV[kTimesV[i]] = i;

    bool useTans = (tanU || tanV) &&
                   (interp == ir::InterpolationType::Hermite ||
                    interp == ir::InterpolationType::Bezier);

    ir::Track<Point3> track;
    track.interpolation = interp;

    for (TimeValue t : times) {
        float u = evalFloat(ctrlU, t, defaultU);
        float v = evalFloat(ctrlV, t, defaultV);

        track.keys.push_back({});
        auto& key = track.keys.back();
        key.time  = t;
        key.value = Point3(u * negateFactor, v, defaultW);

        if (useTans) {
            float uiIn = 0.0f, uiOut = 0.0f, viIn = 0.0f, viOut = 0.0f;
            auto itU = tanIdxU.find(t);
            if (itU != tanIdxU.end() && tanU) {
                uiIn  = kTiU[itU->second] * negateFactor;
                uiOut = kToU[itU->second] * negateFactor;
            }
            auto itV = tanIdxV.find(t);
            if (itV != tanIdxV.end() && tanV) {
                viIn  = kTiV[itV->second];
                viOut = kToV[itV->second];
            }
            key.inTangent  = Point3(uiIn,  viIn,  0.0f);
            key.outTangent = Point3(uiOut, viOut, 0.0f);
            key.hasTangents = true;
        }
    }

    int32_t idx = static_cast<int32_t>(model.vec3Tracks.size());
    model.vec3Tracks.push_back(std::move(track));
    return idx;
}

// Extract UV rotation from the W-angle controller (degrees → quaternion
// around Z). Uses Linear interpolation, or DontInterp for step keys;
// Bezier tangent→quat conversion is non-trivial and rarely needed for UV
// rotation.
static int32_t extractQuatFromAngleCtrl(Control* ctrl, ir::IRModel& model)
{
    if (!ctrl) return -1;
    auto keyTimes = collectKeyTimes(ctrl);
    if (keyTimes.empty()) return -1;

    ir::Track<Quat> track;
    track.interpolation = isStepBezierController(ctrl) ? ir::InterpolationType::None
                                                       : ir::InterpolationType::Linear;

    for (TimeValue t : keyTimes) {
        float angleDeg = evalFloat(ctrl, t, 0.0f);
        float angleRad = angleDeg * (3.14159265358979323846f / 180.0f);

        ir::Keyframe<Quat> key;
        key.time  = t;
        key.value = Quat(0.0f, 0.0f, sinf(angleRad * 0.5f), cosf(angleRad * 0.5f));
        track.keys.push_back(key);
    }

    int32_t idx = static_cast<int32_t>(model.quatTracks.size());
    model.quatTracks.push_back(std::move(track));
    return idx;
}

// The layer alpha controller: "opacity" on Wc3Material, "Alpha" on the NeoDex
// material (same 0..100 range).
static Control* opacityController(ReferenceTarget* mtlRef)
{
    if (Control* c = getParamController(mtlRef, L"opacity")) return c;
    return getParamController(mtlRef, L"Alpha");
}

// Extract the opacity track (KMTA) from the "opacity" param. Values are
// stored 0..100 in the Wc3Material; MDX expects 0..1.

static int32_t extractOpacityTrack(ReferenceTarget* mtlRef,
                                   ir::InterpolationType interp,
                                   ir::IRModel& model)
{
    Control* ctrl = opacityController(mtlRef);
    if (!ctrl) return -1;

    std::vector<TimeValue> times;
    std::vector<float> values, inTans, outTans;
    bool hasTangents = false;
    readFloatKeys(ctrl, times, values, inTans, outTans, hasTangents);
    if (times.empty()) return -1;

    bool useTans = hasTangents &&
                   (interp == ir::InterpolationType::Hermite ||
                    interp == ir::InterpolationType::Bezier);

    ir::Track<float> track;
    track.interpolation = interp;

    for (size_t i = 0; i < times.size(); i++) {
        ir::Keyframe<float> key;
        key.time       = times[i];
        key.value      = values[i] / 100.0f;
        if (useTans) {
            key.inTangent   = inTans[i]  / 100.0f;
            key.outTangent  = outTans[i] / 100.0f;
            key.hasTangents = true;
        }
        track.keys.push_back(key);
    }

    int32_t idx = static_cast<int32_t>(model.floatTracks.size());
    model.floatTracks.push_back(std::move(track));
    return idx;
}

// Interpolation for an animated material param: step-keyed bezier is how the
// importer stores DontInterp, and anything unrecognised is sampled linearly.
static ir::InterpolationType materialTrackInterp(Control* ctrl)
{
    ir::InterpolationType interp = detectInterpFromController(ctrl);
    if (interp == ir::InterpolationType::Bezier && isStepBezierController(ctrl))
        return ir::InterpolationType::None;
    if (interp == ir::InterpolationType::None)
        return ir::InterpolationType::Linear;
    return interp;
}

static int32_t detectAndRegisterGlobalSeq(Control* ctrl, ir::IRModel& model);
static int32_t detectGlobalSeqAny(ir::IRModel& model,
                                  std::initializer_list<Control*> ctrls);

// KMTE / KFCA / KFTC: one animated float param of the Wc3Material.
static int32_t extractMaterialFloatTrack(ReferenceTarget* mtlRef, const wchar_t* name,
                                         ir::IRModel& model)
{
    Control* ctrl = getParamController(mtlRef, name);
    if (!ctrl) return -1;

    std::vector<TimeValue> times;
    std::vector<float> values, inTans, outTans;
    bool hasTangents = false;
    readFloatKeys(ctrl, times, values, inTans, outTans, hasTangents);
    if (times.empty()) return -1;

    ir::Track<float> track;
    track.interpolation = materialTrackInterp(ctrl);
    const bool useTans = hasTangents &&
                         (track.interpolation == ir::InterpolationType::Hermite ||
                          track.interpolation == ir::InterpolationType::Bezier);
    for (size_t i = 0; i < times.size(); i++) {
        ir::Keyframe<float> key;
        key.time  = times[i];
        key.value = values[i];
        if (useTans) {
            key.inTangent   = inTans[i];
            key.outTangent  = outTans[i];
            key.hasTangents = true;
        }
        track.keys.push_back(key);
    }
    track.globalSequenceIndex = detectAndRegisterGlobalSeq(ctrl, model);

    int32_t idx = static_cast<int32_t>(model.floatTracks.size());
    model.floatTracks.push_back(std::move(track));
    return idx;
}

// KFC3: the Wc3Material keeps the fresnel colour as three floats, so the keys
// of the three controllers are merged into one colour track. A channel without
// a key at a merged time is evaluated there and gets flat tangents.
static int32_t extractFresnelColorTrack(ReferenceTarget* mtlRef, const Point3& staticColor,
                                        ir::IRModel& model)
{
    static const wchar_t* const kNames[3] = { L"fresnelR", L"fresnelG", L"fresnelB" };
    Control* ctrls[3] = {};
    std::vector<TimeValue> keyTimes[3];
    std::vector<float> keyValues[3], keyIn[3], keyOut[3];
    bool keyTans[3] = { false, false, false };
    std::vector<TimeValue> times;
    Control* firstAnimated = nullptr;

    for (int c = 0; c < 3; c++) {
        ctrls[c] = getParamController(mtlRef, kNames[c]);
        readFloatKeys(ctrls[c], keyTimes[c], keyValues[c], keyIn[c], keyOut[c], keyTans[c]);
        if (!keyTimes[c].empty() && !firstAnimated) firstAnimated = ctrls[c];
        times.insert(times.end(), keyTimes[c].begin(), keyTimes[c].end());
    }
    if (times.empty()) return -1;
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    ir::Track<Color> track;
    track.interpolation = materialTrackInterp(firstAnimated);
    const bool useTans = (track.interpolation == ir::InterpolationType::Hermite ||
                          track.interpolation == ir::InterpolationType::Bezier);

    for (TimeValue t : times) {
        ir::Keyframe<Color> key;
        key.time = t;
        float value[3], in[3], out[3];
        for (int c = 0; c < 3; c++) {
            value[c] = evalFloat(ctrls[c], t, staticColor[c]);
            in[c] = out[c] = value[c];
            auto it = std::find(keyTimes[c].begin(), keyTimes[c].end(), t);
            if (it != keyTimes[c].end() && keyTans[c]) {
                size_t k = static_cast<size_t>(it - keyTimes[c].begin());
                in[c]  = keyIn[c][k];
                out[c] = keyOut[c][k];
            }
        }
        key.value = Color(value[0], value[1], value[2]);
        if (useTans) {
            key.inTangent   = Color(in[0], in[1], in[2]);
            key.outTangent  = Color(out[0], out[1], out[2]);
            key.hasTangents = true;
        }
        track.keys.push_back(key);
    }
    track.globalSequenceIndex = detectGlobalSeqAny(model, { ctrls[0], ctrls[1], ctrls[2] });

    int32_t idx = static_cast<int32_t>(model.colorTracks.size());
    model.colorTracks.push_back(std::move(track));
    return idx;
}

// ── Global Sequence detection ─────────────────────────────────────
// A controller is a Global Sequence when its after-ORT is set to a
// cyclic behaviour: ORT_CYCLE (2) or ORT_LOOP (3). These are treated
// identically in Max SDK's own controller code (see how Control::GetValue
// handles them — both dispatch to CycleTime). MaxScript's `#cycle`
// maps to ORT_CYCLE (2), but some scenes end up with ORT_LOOP, so we
// accept both.
//
// The duration is the time of the last key.
//
// The IRModel maintains `globalSequenceDurations` — each unique
// duration gets an index. Tracks that share a duration share an index.
// The model builder writes this as the GLBS chunk.

// Check if a controller is a Global Sequence. Returns the duration
// (time of last key) in TimeValue ticks, or 0 if not a Global Sequence.
static TimeValue getGlobalSequenceDuration(Control* ctrl) {
    if (!ctrl) return 0;

    // Max SDK: GetORT(ORT_AFTER) returns the after-range behaviour.
    // MaxScript's `#cycle` maps to SDK's ORT_CYCLE (2). ORT_LOOP (3)
    // is treated identically for cycle semantics — accept both.
    int afterORT = ctrl->GetORT(ORT_AFTER);
    if (afterORT != ORT_CYCLE && afterORT != ORT_LOOP) return 0;

    // Duration = time of last key
    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (!ikc) return 0;
    int n = ikc->GetNumKeys();
    if (n == 0) return 0;

    // All float key struct layouts start with TimeValue, so reading
    // as IBezFloatKey is safe just for the time field.
    IBezFloatKey key;
    ikc->GetKey(n - 1, &key);
    return key.time;
}

// Register a duration with the IRModel, returning its 0-based index.
// Identical durations are merged — they share the same index.
static int32_t registerGlobalSequence(ir::IRModel& model, TimeValue duration) {
    if (duration <= 0) return -1;
    uint32_t d = static_cast<uint32_t>(duration);

    for (size_t i = 0; i < model.globalSequenceDurations.size(); i++) {
        if (model.globalSequenceDurations[i] == d)
            return static_cast<int32_t>(i);
    }

    model.globalSequenceDurations.push_back(d);
    return static_cast<int32_t>(model.globalSequenceDurations.size() - 1);
}

// Convenience: detect + register for a single controller. Returns -1
// if the controller isn't a Global Sequence.
static int32_t detectAndRegisterGlobalSeq(Control* ctrl, ir::IRModel& model) {
    TimeValue dur = getGlobalSequenceDuration(ctrl);
    if (dur == 0) return -1;
    return registerGlobalSequence(model, dur);
}

// Scan several controllers and register the first Global Sequence found.
// Used when multiple source controllers (U+V offset, etc.) feed one track.
static int32_t detectGlobalSeqAny(ir::IRModel& model,
                                  std::initializer_list<Control*> ctrls)
{
    for (Control* c : ctrls) {
        int32_t idx = detectAndRegisterGlobalSeq(c, model);
        if (idx >= 0) return idx;
    }
    return -1;
}

// ── Three-float Vec3 extractor (U, V, W) ──────────────────────────
// Used for UV translation where MDX's 3rd component is a real
// animatable W_Offset stored on the Wc3Bitmap as `anim_WOffset`.
// (Max's StdUVGen has no W_Offset — this param is MDX round-trip only.)
// signA is applied to the first (U) component — MDX X = -U_Offset.
static int32_t extractVec3FromThreeCtrls(Control* ctrlA, Control* ctrlB,
                                         Control* ctrlC,
                                         float defaultA, float defaultB, float defaultC,
                                         ir::InterpolationType interp,
                                         ir::IRModel& model,
                                         float signA = 1.0f)
{
    if (!ctrlA && !ctrlB && !ctrlC) return -1;

    // Read per-controller keys with tangents
    std::vector<TimeValue> kTimesA, kTimesB, kTimesC;
    std::vector<float> kValsA, kValsB, kValsC;
    std::vector<float> kTiA, kToA, kTiB, kToB, kTiC, kToC;
    bool tanA = false, tanB = false, tanC = false;
    readFloatKeys(ctrlA, kTimesA, kValsA, kTiA, kToA, tanA);
    readFloatKeys(ctrlB, kTimesB, kValsB, kTiB, kToB, tanB);
    readFloatKeys(ctrlC, kTimesC, kValsC, kTiC, kToC, tanC);

    if (kTimesA.empty() && kTimesB.empty() && kTimesC.empty()) return -1;

    std::vector<TimeValue> times;
    times.reserve(kTimesA.size() + kTimesB.size() + kTimesC.size());
    for (auto t : kTimesA) times.push_back(t);
    for (auto t : kTimesB) times.push_back(t);
    for (auto t : kTimesC) times.push_back(t);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    // Tangent lookup by time
    std::unordered_map<TimeValue, size_t> tanIdxA, tanIdxB, tanIdxC;
    for (size_t i = 0; i < kTimesA.size(); i++) tanIdxA[kTimesA[i]] = i;
    for (size_t i = 0; i < kTimesB.size(); i++) tanIdxB[kTimesB[i]] = i;
    for (size_t i = 0; i < kTimesC.size(); i++) tanIdxC[kTimesC[i]] = i;

    bool useTans = (tanA || tanB || tanC) &&
                   (interp == ir::InterpolationType::Hermite ||
                    interp == ir::InterpolationType::Bezier);

    ir::Track<Point3> track;
    track.interpolation = interp;

    for (TimeValue t : times) {
        float a = evalFloat(ctrlA, t, defaultA) * signA;
        float b = evalFloat(ctrlB, t, defaultB);
        float c = evalFloat(ctrlC, t, defaultC);

        track.keys.push_back({});
        auto& key = track.keys.back();
        key.time  = t;
        key.value = Point3(a, b, c);

        if (useTans) {
            float aiIn = 0.0f, aiOut = 0.0f;
            float biIn = 0.0f, biOut = 0.0f;
            float ciIn = 0.0f, ciOut = 0.0f;
            auto itA = tanIdxA.find(t);
            if (itA != tanIdxA.end() && tanA) {
                aiIn  = kTiA[itA->second] * signA;
                aiOut = kToA[itA->second] * signA;
            }
            auto itB = tanIdxB.find(t);
            if (itB != tanIdxB.end() && tanB) {
                biIn  = kTiB[itB->second];
                biOut = kToB[itB->second];
            }
            auto itC = tanIdxC.find(t);
            if (itC != tanIdxC.end() && tanC) {
                ciIn  = kTiC[itC->second];
                ciOut = kToC[itC->second];
            }
            key.inTangent  = Point3(aiIn,  biIn,  ciIn);
            key.outTangent = Point3(aiOut, biOut, ciOut);
            key.hasTangents = true;
        }
    }

    int32_t idx = static_cast<int32_t>(model.vec3Tracks.size());
    model.vec3Tracks.push_back(std::move(track));
    return idx;
}

// Per-export dedup cache, keyed by diffuseMap pointer.
//
// When multiple material layers share the same texture instance (composite
// materials), they share the same UV-animation semantics because the
// controllers live on the texture (Wc3Bitmap params / bitmap coords), not
// per-layer. Dedup by the texmap pointer — the texture is the source of
// truth, and the importer clones bitmaps for layers whose MDX texture
// animations actually differ.
//
// Reset at the start of each top-level extractMaterials() call.
static std::unordered_map<Texmap*, int32_t> g_texAnimCache;

// Interpolation for a TXAN track built from several UV controllers. The
// importer splits an MDX DontInterp track (sprite-sheet flip-books) into
// step-keyed bezier controllers, so the track is DontInterp when every keyed
// controller is step-keyed. Otherwise the first controller decides, and
// anything unrecognised is sampled linearly.
static ir::InterpolationType uvTrackInterp(std::initializer_list<Control*> ctrls)
{
    bool anyKeyed = false, allStep = true;
    Control* first = nullptr;
    for (Control* c : ctrls) {
        if (!c) continue;
        if (!first) first = c;
        if (c->NumKeys() <= 0) continue;
        anyKeyed = true;
        if (!isStepBezierController(c)) allStep = false;
    }
    if (anyKeyed && allStep) return ir::InterpolationType::None;
    ir::InterpolationType interp = detectInterpFromController(first);
    if (interp == ir::InterpolationType::None)
        interp = ir::InterpolationType::Linear;
    return interp;
}

// Extract TextureAnimation for a Wc3Material layer. The controllers are
// resolved from the layer's diffuse texmap (Wc3Bitmap anim_* params, with
// StdUVGen / legacy-material fallbacks — see resolveUVAnimControllers).
// Returns the TextureAnimation index, or -1 if no UV animation controllers
// are present. Uses g_texAnimCache (keyed by the diffuse texmap pointer) to
// deduplicate across composite-material layers that share the same bitmap.
static int32_t extractTextureAnimationFromMaterial(ReferenceTarget* mtlRef,
                                                   ir::IRModel& model)
{
    if (!mtlRef) return -1;

    Texmap* diffuseTexForAnim = nullptr;
    readTexmapFB(mtlRef, L"diffuseMap", L"texture", diffuseTexForAnim);

    UVAnimControllers uv = resolveUVAnimControllers(mtlRef, diffuseTexForAnim);
    if (!uv.any()) return -1;

    // Cache lookup by diffuseMap pointer
    if (diffuseTexForAnim) {
        auto cached = g_texAnimCache.find(diffuseTexForAnim);
        if (cached != g_texAnimCache.end())
            return cached->second;
    }

    ir::TextureAnimation ta;

    // Translation: U offset + V offset + W offset → Vec3(U, V, W), U negated
    if (uv.uOffset || uv.vOffset || uv.wOffset) {
        const ir::InterpolationType interp =
            uvTrackInterp({uv.uOffset, uv.vOffset, uv.wOffset});

        ta.translationTrackIndex = extractVec3FromThreeCtrls(
            uv.uOffset, uv.vOffset, uv.wOffset,
            0.0f, 0.0f, 0.0f,
            interp, model,
            -1.0f);  // negate U: MDX X = -U_Offset (matches importer)

        if (ta.translationTrackIndex >= 0) {
            int32_t gsIdx = detectGlobalSeqAny(model, {uv.uOffset, uv.vOffset, uv.wOffset});
            if (gsIdx >= 0)
                model.vec3Tracks[ta.translationTrackIndex].globalSequenceIndex = gsIdx;
        }
    }

    // Rotation: W angle → Quat (Linear)
    if (uv.wAngle) {
        ta.rotationTrackIndex = extractQuatFromAngleCtrl(uv.wAngle, model);
        if (ta.rotationTrackIndex >= 0) {
            int32_t gsIdx = detectAndRegisterGlobalSeq(uv.wAngle, model);
            if (gsIdx >= 0)
                model.quatTracks[ta.rotationTrackIndex].globalSequenceIndex = gsIdx;
        }
    }

    // Scale: U tiling + V tiling → Vec3(U, V, 1)
    if (uv.uTiling || uv.vTiling) {
        const ir::InterpolationType interp =
            uvTrackInterp({uv.uTiling, uv.vTiling});

        ta.scaleTrackIndex = extractVec3FromTwoCtrls(
            uv.uTiling, uv.vTiling,
            1.0f, 1.0f, 1.0f,
            interp, model);

        if (ta.scaleTrackIndex >= 0) {
            int32_t gsIdx = detectGlobalSeqAny(model, {uv.uTiling, uv.vTiling});
            if (gsIdx >= 0)
                model.vec3Tracks[ta.scaleTrackIndex].globalSequenceIndex = gsIdx;
        }
    }

    if (ta.translationTrackIndex < 0 &&
        ta.rotationTrackIndex < 0 &&
        ta.scaleTrackIndex < 0)
    {
        return -1;
    }

    int32_t taIdx = static_cast<int32_t>(model.textureAnimations.size());
    model.textureAnimations.push_back(std::move(ta));
    if (diffuseTexForAnim)
        g_texAnimCache[diffuseTexForAnim] = taIdx;
    return taIdx;
}

// Material-level properties extracted from a single Wc3Material sub-material.
// These get merged when building a composite material.
struct MaterialLevelProps {
    int priorityPlane = 0;
    uint32_t flags = 0;
    std::string shaderName;
};

// ─── IFL (Image File List) animation extraction ──────────────────────
//
// The importer stores animated textures as a single BitmapTex with a
// .ifl filename. The .ifl is a text file containing one texture path
// per line. When the exporter sees such a BitmapTex, it must reverse
// the importer's encoding to recover a KMTF track with per-texture keys.
//
// Importer encoding (see exporter_handoff_ifl_sequence.md §1):
//   bmpTex->startTime     = first key's Max tick (usually 0)
//   bmpTex->playbackRate  = TicksPerFrame / avgInterval_ticks
//   bmpTex->endCondition  = 0 (LOOP, when MDX used a GlobalSequence)
//                         | 2 (HOLD, when MDX used a named sequence)
//
// Reverse for export:
//   avgInterval_ticks = TicksPerFrame / playbackRate
//   key[i].time (ticks) = startTime + i * avgInterval_ticks
//   The builder then runs ticksToMs on the track, producing the
//   final MDX key times in ms.

// Return true if `fname` ends in ".ifl" (case-insensitive).
bool isIflFilename(const std::string& fname) {
    if (fname.size() < 4) return false;
    const char* s = fname.c_str() + fname.size() - 4;
    return (std::tolower(static_cast<unsigned char>(s[0])) == '.' &&
            std::tolower(static_cast<unsigned char>(s[1])) == 'i' &&
            std::tolower(static_cast<unsigned char>(s[2])) == 'f' &&
            std::tolower(static_cast<unsigned char>(s[3])) == 'l');
}

// Read the IFL file's lines as texture paths. Returns empty vector if
// the file can't be opened or is empty. Trims CR/LF/whitespace from
// each line and skips blank lines.
std::vector<std::string> readIflLines(const std::string& iflPath) {
    std::vector<std::string> out;
    std::ifstream ifs(iflPath);
    if (!ifs.is_open()) return out;

    std::string line;
    while (std::getline(ifs, line)) {
        while (!line.empty() && (line.back() == '\r' ||
                                  line.back() == '\n' ||
                                  line.back() == ' '  ||
                                  line.back() == '\t'))
            line.pop_back();
        // Trim leading whitespace too (uncommon but safe)
        size_t start = line.find_first_not_of(" \t");
        if (start != std::string::npos && start > 0)
            line.erase(0, start);
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

// Extract the filename component (no directory) from a path string.
// Mirrors extractBitmapFileName's stripPath lambda but for std::string.
std::string filenameOnly(const std::string& path) {
    if (path.empty()) return path;
    size_t lastSep = std::string::npos;
    for (size_t i = path.size(); i > 0; --i) {
        char c = path[i - 1];
        if (c == '\\' || c == '/') { lastSep = i - 1; break; }
    }
    return (lastSep != std::string::npos) ? path.substr(lastSep + 1) : path;
}

// Extract an IFL animation track from a BitmapTex whose filename points
// to a .ifl file. Registers each IFL line as a TEXS entry (if not already
// present), builds an IntTrack of texture-indices, optionally binds it to
// a GlobalSequence, and stores it in `model.intTracks`.
//
// Returns the track index to put on `layer.textureIdTrackIndex`, or -1 if
// extraction fails (IFL file missing / < 2 entries / invalid playbackRate).
// On success, `outFirstTexIndex` receives the first IFL texture's index so
// the caller can still set a static `layer.textureRefs[0]` to something
// sensible (most tools ignore it when a KMTF is present, but some validate it).
// `prefix` is the material's MDX folder for the frames (NeoDex keeps it in
// the material's `path`); each frame's IFL line is also its conversion source.
int32_t extractIflAnimation(BitmapTex* bmpTex, ir::IRModel& model,
                             int32_t defaultReplaceableId,
                             bool defaultWrapU, bool defaultWrapV,
                             int32_t& outFirstTexIndex,
                             const std::string& prefix = std::string())
{
    outFirstTexIndex = -1;
    if (!bmpTex) return -1;

    // Read the .ifl path from the BitmapTex
    std::string iflPath;
    {
        const MCHAR* mp = bmpTex->GetMapName();
        if (!mp) return -1;
        iflPath = wstrToUtf8(mp);
    }
    if (!isIflFilename(iflPath)) return -1;

    // Read the .ifl file's contents — one texture path per line
    std::vector<std::string> lines = readIflLines(iflPath);
    // A frame as the MDX names it, and the file it comes from (IFL lines may
    // be relative to the IFL's own folder).
    auto mdxPathOf = [&](const std::string& line) {
        return buildTexturePath(prefix, filenameOnly(line));
    };
    auto diskPathOf = [&](const std::string& line) {
        const std::filesystem::path p(utf8ToWide(line));
        if (p.is_absolute()) return line;
        return wstrToUtf8((std::filesystem::path(utf8ToWide(iflPath)).parent_path() / p)
                              .wstring().c_str());
    };
    if (lines.size() < 2) {
        // Not an animated IFL — fall back to static texture using the
        // first line if any, otherwise caller's static path.
        if (lines.size() == 1) {
            outFirstTexIndex = findOrAddTexture(model,
                mdxPathOf(lines[0]),
                defaultReplaceableId, defaultWrapU, defaultWrapV,
                diskPathOf(lines[0]));
        }
        return -1;
    }

    // Read the importer-stored parameters
    TimeValue startTime = bmpTex->GetStartTime();
    float playbackRate  = bmpTex->GetPlaybackRate();
    int   endCondition  = bmpTex->GetEndCondition();

    // Guard against invalid playback rate
    if (playbackRate <= 0.0001f) {
        // Invalid / unset — can't reconstruct timing. Fall back to 1 key.
        outFirstTexIndex = findOrAddTexture(model,
            mdxPathOf(lines[0]),
            defaultReplaceableId, defaultWrapU, defaultWrapV,
            diskPathOf(lines[0]));
        return -1;
    }

    // Reverse the importer formula:
    //   playbackRate = TicksPerFrame / avgInterval_ticks
    //   avgInterval_ticks = TicksPerFrame / playbackRate
    TimeValue tpf = GetTicksPerFrame();
    TimeValue avgIntervalTicks = static_cast<TimeValue>(
        static_cast<float>(tpf) / playbackRate + 0.5f);
    if (avgIntervalTicks <= 0) avgIntervalTicks = tpf;  // safety

    // Map each IFL line to a TEXS index (create if missing)
    std::vector<uint32_t> texIndices;
    texIndices.reserve(lines.size());
    for (const std::string& raw : lines) {
        int32_t idx = findOrAddTexture(model, mdxPathOf(raw),
            defaultReplaceableId, defaultWrapU, defaultWrapV, diskPathOf(raw));
        texIndices.push_back(static_cast<uint32_t>(idx));
    }
    outFirstTexIndex = static_cast<int32_t>(texIndices[0]);

    // Build the IntTrack — one key per texture, evenly spaced
    ir::IntTrack track;
    track.interpolation = ir::InterpolationType::Linear;

    // endCondition 0 (LOOP) → GlobalSequence binding
    // endCondition 2 (HOLD) → no global sequence (plays once within
    //                          a named sequence's duration)
    // endCondition 1 (PINGPONG) is not representable in MDX; we
    //                          treat it as LOOP.
    if (endCondition == 0 || endCondition == 1) {
        TimeValue totalDuration = avgIntervalTicks *
            static_cast<TimeValue>(texIndices.size());
        int32_t gsIdx = core::anim::registerGlobalSequence(model, totalDuration);
        if (gsIdx >= 0) track.globalSequenceIndex = gsIdx;
    }

    for (size_t i = 0; i < texIndices.size(); ++i) {
        ir::Keyframe<int32_t> key;
        key.time = startTime +
            static_cast<TimeValue>(i) * avgIntervalTicks;
        key.value = static_cast<int32_t>(texIndices[i]);
        track.keys.push_back(key);
    }

    int32_t trackIdx = static_cast<int32_t>(model.intTracks.size());
    model.intTracks.push_back(std::move(track));
    return trackIdx;
}

// Exact KMTF from Wc3Material's flipbookTextures / flipbookFrame, which the
// importer writes next to the IFL preview. Each entry is
// "replaceableId|flags|MDX path"; flipbookFrame is the animated index into
// them. Returns the int track index, or -1 when flipbookFrame has no keys.
// `iflDiskPaths` are the IFL's absolute file paths: an entry whose file name
// matches one takes it as its source on disk, so texture conversion still
// finds the file. `outStaticTexIndex` is the texture shown at frame 0.
static int32_t extractExactFlipbook(ReferenceTarget* mtlRef, ir::IRModel& model,
                                    const std::vector<std::string>& iflDiskPaths,
                                    int32_t& outStaticTexIndex)
{
    outStaticTexIndex = -1;
    IParamBlock2* pb = nullptr;
    ParamID pid = 0;
    if (!findAnimParam(mtlRef, L"flipbookTextures", pb, pid)) return -1;
    const int count = pb->Count(pid);
    if (count <= 0) return -1;
    Control* frameCtrl = getParamController(mtlRef, L"flipbookFrame");
    if (!frameCtrl) return -1;
    const std::vector<TimeValue> times = collectKeyTimes(frameCtrl);
    if (times.empty()) return -1;

    auto stem = [](const std::string& path) {
        std::string s = filenameOnly(path);
        const size_t dot = s.find_last_of('.');
        if (dot != std::string::npos) s.resize(dot);
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    std::vector<int32_t> texIndex(static_cast<size_t>(count), -1);
    for (int i = 0; i < count; ++i) {
        const MCHAR* raw = pb->GetStr(pid, 0, i);
        const std::string entry = raw ? wdx::text::wideToMdx(raw) : std::string();
        const size_t a = entry.find('|');
        const size_t b = (a == std::string::npos) ? std::string::npos : entry.find('|', a + 1);
        if (b == std::string::npos) continue;
        const int replaceableId = std::atoi(entry.substr(0, a).c_str());
        const int flags = std::atoi(entry.substr(a + 1, b - a - 1).c_str());
        std::string path = entry.substr(b + 1);
        std::string disk;
        if (!path.empty()) {
            for (const auto& line : iflDiskPaths)
                if (stem(line) == stem(path)) { disk = line; break; }
        }
        // Every other bitmap exports as its MDX folder + the file it loaded
        // from (an HD .dds for a .blp reference, say). Do the same, or the
        // flipbook and the static diffuse name one texture twice.
        if (!disk.empty()) {
            const size_t sep = path.find_last_of("\\/");
            path = (sep == std::string::npos ? std::string() : path.substr(0, sep + 1)) +
                   filenameOnly(disk);
        }
        texIndex[static_cast<size_t>(i)] = findOrAddTexture(model, path, replaceableId,
            (flags & 1) != 0, (flags & 2) != 0, disk);
    }

    auto textureAt = [&](TimeValue t) {
        const long frame = std::lround(evalFloat(frameCtrl, t));
        return texIndex[static_cast<size_t>(std::clamp<long>(frame, 0, count - 1))];
    };

    ir::IntTrack track;
    track.interpolation = materialTrackInterp(frameCtrl);
    if (track.interpolation != ir::InterpolationType::None)
        track.interpolation = ir::InterpolationType::Linear;
    for (TimeValue t : times) {
        const int32_t tex = textureAt(t);
        if (tex < 0) continue;
        ir::Keyframe<int32_t> key;
        key.time = t;
        key.value = tex;
        track.keys.push_back(key);
    }
    if (track.keys.empty()) return -1;

    outStaticTexIndex = textureAt(0);
    track.globalSequenceIndex = detectAndRegisterGlobalSeq(frameCtrl, model);
    int32_t idx = static_cast<int32_t>(model.intTracks.size());
    model.intTracks.push_back(std::move(track));
    return idx;
}

// `textureLayerOnly`: export the diffuse as an ordinary texture even when the
// material is a Team Color replaceable — the upper layer of the NeoDex Team
// Color split (see extractWc3Material).
// The Wc3Material "filterMode" dropdown (and Material Fix's FilterMode):
// 1 None, 2 Transparent, 3 Blend, 4 Additive, 5 Add Alpha, 6 Modulate,
// 7 Modulate 2x.
static ir::BlendMode blendModeFromFilterDropdown(int filterMode) {
    switch (filterMode) {
    case 2: return ir::BlendMode::Transparent;
    case 3: return ir::BlendMode::Blend;
    case 4: return ir::BlendMode::Additive;
    case 5: return ir::BlendMode::AddAlpha;
    case 6: return ir::BlendMode::Modulate;
    case 7: return ir::BlendMode::Modulate2x;
    default: return ir::BlendMode::None;
    }
}

ir::MaterialLayer extractWc3Layer(ReferenceTarget* mtlRef, ir::IRModel& model,
                                  MaterialLevelProps& matProps,
                                  bool textureLayerOnly = false)
{
    using PBR = core::ParamBlockReader;
    TimeValue t = 0;
    // The NeoDex "Warcraft 3" material keeps several settings under other
    // names or in another place than Wc3Material; those are read below
    // only for it, so a Wc3Material never picks up a stray NeoDex name.
    const bool neoDex = mtlRef->ClassID() == mdx_ids::NEODEX_MATERIAL;

    ir::MaterialLayer layer;

    // Filter mode (1-based: 1=None..7=Modulate2x)
    int filterMode = 1;
    PBR::readIntByName(mtlRef, L"filterMode", t, filterMode);
    // NeoDex's own Team Color split draws the texture over the team colour,
    // so an opaque texture layer becomes Transparent (NeoDexSceneParser
    // LoadLayers).
    if (textureLayerOnly && filterMode == 1) filterMode = 2;
    layer.blendMode = blendModeFromFilterDropdown(filterMode);

    // Opacity (0–100 → 0.0–1.0)
    float opacity = 100.0f;
    readFloatFB(mtlRef, L"opacity", L"Alpha", t, opacity);
    layer.alpha = opacity / 100.0f;

    // Flags (NeoDex: twosides, NoDephTest, NoDephSet — different spelling)
    BOOL flagVal = FALSE;
    if (readBoolFB(mtlRef, L"twoSided", L"twosides", t, flagVal) && flagVal)
        layer.twoSided = true;
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"unshaded", t, flagVal) && flagVal)
        layer.unshaded = true;
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"unfogged", t, flagVal) && flagVal)
        layer.unfogged = true;
    flagVal = FALSE;
    if (readBoolFB(mtlRef, L"noDepthTest", L"NoDephTest", t, flagVal) && flagVal)
        layer.noDepthTest = true;
    flagVal = FALSE;
    if (readBoolFB(mtlRef, L"noDepthSet", L"NoDephSet", t, flagVal) && flagVal)
        layer.noDepthWrite = true;
    // 3.0.0 layer flags. No NeoDex spelling: NeoDex predates both bits.
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"ambientOcclusion", t, flagVal) && flagVal)
        layer.ambientOcclusion = true;
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"backFacesForShadows", t, flagVal) && flagVal)
        layer.backFacesForShadows = true;
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"unlit", t, flagVal) && flagVal)
        layer.unlit = true;

    // Shader dropdown (1 SD, 2 HD, 3 SD on HD, 4 Crystal) → MDX ShaderType.
    // Only Wc3Material has it: a NeoDex material would answer with its
    // Standard delegate's shader instead, so leave that one to the mapper.
    if (mtlRef->ClassID() == mdx_ids::WC3_MATERIAL) {
        int shaderDropdown = 0;
        if (PBR::readIntByName(mtlRef, L"shaderType", t, shaderDropdown)) {
            switch (shaderDropdown) {
            case 1: layer.shaderType = 0;  break;
            case 2: layer.shaderType = 1;  break;
            case 3: layer.shaderType = 2;  break;
            case 4: layer.shaderType = 24; break;
            default: break;
            }
        }
    }

    // Priority plane
    int priority = 0;
    PBR::readIntByName(mtlRef, L"priorityPlane", t, priority);
    matProps.priorityPlane = priority;

    // Coord ID. -1 is the default of a material made in Max, where the UV set
    // is chosen on the bitmap instead (Map Channel 2 = the second unwrap), so
    // take it from there — the same rule the preview renderer follows. The
    // importer always writes an explicit value, so round trips are unaffected.
    int coordId = -1;
    PBR::readIntByName(mtlRef, L"coordId", t, coordId);
    if (coordId < 0) {
        Texmap* diffuseTex = nullptr;
        readTexmapFB(mtlRef, L"diffuseMap", L"texture", diffuseTex);
        if (BitmapTex* bmt = unwrapBitmapTex(diffuseTex))
            if (StdUVGen* uv = bmt->GetUVGen())
                coordId = uv->GetMapChannel() - 1;
    }
    layer.uvSetIndex = (coordId >= 0) ? coordId : 0;

    // Material flags — ConstantColor
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"constantColor", t, flagVal) && flagVal)
        matProps.flags |= 0x01;

    // Material flags — FullResolution
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"fullResolution", t, flagVal) && flagVal)
        matProps.flags |= 0x20;

    // Material flags — SortOrder (1=unused, 2=nearToFar, 3=farToNear)
    // NeoDex: SortPrimsFarZ
    int sortOrder = 1;
    readIntFB(mtlRef, L"sortOrder", L"SortPrimsFarZ", t, sortOrder);
    if (sortOrder == 2) matProps.flags |= 0x08;
    else if (sortOrder == 3) matProps.flags |= 0x10;

    // Shader path (Reforged)
    std::wstring shaderPath;
    if (PBR::readStringByName(mtlRef, L"shaderPath", t, shaderPath) && !shaderPath.empty())
        matProps.shaderName = wstrToUtf8(shaderPath.c_str());

    // Material-level replaceableId (new NeoDex scheme — field moved from
    // Wc3Bitmap to Wc3Material). The dropdown holds a list position that
    // wdx::replaceable::IdFromDropdown turns into the MDX id (0 none,
    // 1 team colour, 2 team glow, 11 cliff, 31-37 trees).
    //
    // We fall back to reading from the bitmap (extractBitmapProperties) if
    // the material doesn't explicitly set replaceableId — that preserves
    // compatibility with older Wc3Bitmap-based materials that still carry
    // the replaceableId there. Zero means "not set / Not Used".
    int matReplaceableId = 0;  // 0 = Normal / Not Set
    bool matReplaceableIdFound = false;
    {
        int dropdownVal = 0;
        // Wc3Material's dropdown holds a list position (wdx_replaceable_ids.h);
        // NeoDex's "retexture" list is 0 / 1 / 2 itself, so position - 1.
        if (core::ParamBlockReader::readIntByName(mtlRef, L"replaceableId", t, dropdownVal)) {
            matReplaceableIdFound = true;
            matReplaceableId = wdx::replaceable::IdFromDropdown(dropdownVal);
        } else if (core::ParamBlockReader::readIntByName(mtlRef, L"retexture", t, dropdownVal)) {
            matReplaceableIdFound = true;
            matReplaceableId = std::max(0, dropdownVal - 1);
        }
        if (textureLayerOnly) matReplaceableId = 0;

        // Debug log — write to %TEMP%\mdlx_replaceable_debug.log
        {
            char tempPath[MAX_PATH];
            GetTempPathA(MAX_PATH, tempPath);
            std::string logPath = std::string(tempPath) + "mdlx_replaceable_debug.log";
            std::ofstream log(logPath, std::ios::app);
            if (log.is_open()) {
                // Grab the material name via Mtl* interface (MaterialLevelProps
                // doesn't store a name field).
                std::string matName = "<unknown>";
                if (auto* mtl = dynamic_cast<Mtl*>(mtlRef)) {
                    const MCHAR* n = mtl->GetName();
                    if (n) matName = wstrToUtf8(n);
                }
                log << "[MAT] name='" << matName << "'"
                    << " hasReplProp=" << (matReplaceableIdFound ? "yes" : "NO")
                    << " dropdownVal=" << dropdownVal
                    << " -> matReplaceableId=" << matReplaceableId
                    << "\n";
            }
        }
    }

    // --- Diffuse texture ---
    Texmap* texmap = nullptr;
    bool hasTexmap = readTexmapFB(mtlRef, L"diffuseMap", L"texture", texmap);
    if (hasTexmap) {
        std::string fileName = extractBitmapFileName(texmap);
        std::string srcDisk = extractBitmapDiskPath(texmap);
        BitmapProperties bmpProps = extractBitmapProperties(texmap);

        // Material-level replaceableId wins over bitmap-level (new scheme).
        // Only fall back to bitmap value if material didn't override.
        if (matReplaceableId > 0 || textureLayerOnly) {
            bmpProps.replaceableId = matReplaceableId;
        }
        // NeoDex keeps the wrap flags and SphereEnvMap on the material, and
        // its bitmap tiling is crossed (U_Tile holds Wrap_Height), so the
        // material params are the truth.
        if (neoDex) {
            BOOL wrap = FALSE;
            if (PBR::readBoolByName(mtlRef, L"Wrap_Width", t, wrap))  bmpProps.wrapU = wrap != 0;
            wrap = FALSE;
            if (PBR::readBoolByName(mtlRef, L"Wrap_Height", t, wrap)) bmpProps.wrapV = wrap != 0;
            BOOL env = FALSE;
            if (PBR::readBoolByName(mtlRef, L"SphereEnvMap", t, env) && env)
                bmpProps.sphereEnvMap = true;
        }

        // For every replaceable texture (team colour / glow, cliff, trees -
        // wdx_replaceable_ids.h) the MDX convention is an empty texture
        // path: the game substitutes the texture at runtime based on the
        // replaceableId. Force the path empty, regardless of what diffuseMap
        // points to (the importer loads the canonical texture there so the
        // viewport shows something).
        const bool replaceable = bmpProps.replaceableId > 0;
        if (replaceable) {
            fileName.clear();
        }

        // Path prefix: per-slot prefix lives on the Wc3Material
        // (new scheme), but fall back to the bitmap's own prefixPath
        // for legacy materials that still carry it on the bitmap.
        std::string matPrefix = readMaterialPrefix(mtlRef, L"diffusePrefix");
        if (matPrefix.empty() && neoDex) matPrefix = readMaterialPrefix(mtlRef, L"path");
        std::string prefix = !matPrefix.empty() ? matPrefix : bmpProps.prefixPath;
        std::string texPath = replaceable
                                  ? std::string()  // empty path for replaceables
                                  : buildTexturePath(prefix, fileName);

        // Extended debug log for diffuse texture processing
        {
            char tempPath[MAX_PATH];
            GetTempPathA(MAX_PATH, tempPath);
            std::string logPath = std::string(tempPath) + "mdlx_replaceable_debug.log";
            std::ofstream log(logPath, std::ios::app);
            if (log.is_open()) {
                std::string texClass = "unknown";
                if (texmap->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) texClass = "BitmapTex";
                else if (texmap->ClassID() == mdx_ids::WC3_BITMAP) texClass = "Wc3Bitmap";
                log << "  [DIFF] texClass=" << texClass
                    << " fileName='" << fileName << "'"
                    << " bmpReplId=" << bmpProps.replaceableId
                    << " matPrefix='" << matPrefix << "'"
                    << " bmpPrefix='" << bmpProps.prefixPath << "'"
                    << " -> texPath='" << texPath << "'"
                    << " finalReplId=" << bmpProps.replaceableId
                    << "\n";
            }
        }

        // SphereEnvMap lives on the bitmap
        if (bmpProps.sphereEnvMap) layer.sphereEnvMap = true;

        // ── IFL detection ─────────────────────────────────────────
        // If the BitmapTex points to a .ifl file, the importer stored
        // an animated texture sequence. Reverse-engineer it into a
        // KMTF track now. The function returns -1 for non-IFL or
        // single-frame IFLs, in which case we fall through to the
        // static-texture path below.
        int32_t iflTrackIdx = -1;
        int32_t iflFirstTex = -1;
        BitmapTex* bmpTexForIfl = nullptr;
        if (texmap->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
            bmpTexForIfl = static_cast<BitmapTex*>(texmap);
        } else if (texmap->ClassID() == mdx_ids::WC3_BITMAP) {
            // Wc3Bitmap wraps a BitmapTex delegate
            for (int i = 0; i < texmap->NumRefs(); i++) {
                ReferenceTarget* ref = texmap->GetReference(i);
                if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                    bmpTexForIfl = static_cast<BitmapTex*>(ref);
                    break;
                }
            }
        }
        // The exact flipbook the importer stored wins; the IFL is read back
        // only for scenes without it (older imports, hand-made IFLs).
        {
            std::vector<std::string> iflDiskPaths;
            if (bmpTexForIfl && bmpTexForIfl->GetMapName()) {
                const std::string mapName = wstrToUtf8(bmpTexForIfl->GetMapName());
                if (isIflFilename(mapName))
                    iflDiskPaths = readIflLines(mapName);
            }
            iflTrackIdx = extractExactFlipbook(mtlRef, model, iflDiskPaths, iflFirstTex);
        }
        if (iflTrackIdx < 0 && bmpTexForIfl) {
            iflTrackIdx = extractIflAnimation(bmpTexForIfl, model,
                bmpProps.replaceableId, bmpProps.wrapU, bmpProps.wrapV,
                iflFirstTex, prefix);
        }

        if (iflTrackIdx >= 0) {
            // IFL animation — KMTF track created. Set the static
            // textureRef to the first IFL texture (most tools ignore
            // this when KMTF is present, but some validate it exists).
            layer.textureIdTrackIndex = iflTrackIdx;
            ir::TextureRef texRef;
            texRef.textureIndex = (iflFirstTex >= 0)
                ? iflFirstTex
                : findOrAddTexture(model, texPath,
                    bmpProps.replaceableId, bmpProps.wrapU, bmpProps.wrapV,
                    srcDisk);
            texRef.slot = ir::TextureSlot::Diffuse;
            layer.textureRefs.push_back(texRef);
        } else {
            // Static diffuse — original behaviour
            ir::TextureRef texRef;
            texRef.textureIndex = findOrAddTexture(model, texPath,
                bmpProps.replaceableId, bmpProps.wrapU, bmpProps.wrapV,
                srcDisk);
            texRef.slot = ir::TextureSlot::Diffuse;
            layer.textureRefs.push_back(texRef);
        }
    } else if (matReplaceableId > 0) {
        // Material has no diffuseMap but is a replaceable (team colour /
        // glow, cliff, tree). MDX convention is to emit a TEXS entry with the
        // replaceableId set and an empty path — the engine substitutes the
        // texture at runtime. Without this fallback, replaceable materials
        // that don't carry a bitmap lose their replaceableId on export.
        ir::TextureRef texRef;
        texRef.textureIndex = findOrAddTexture(model,
            /*path=*/std::string(),
            matReplaceableId,
            /*wrapU=*/false, /*wrapV=*/false);
        texRef.slot = ir::TextureSlot::Diffuse;
        layer.textureRefs.push_back(texRef);
    }

    // ────────────────────────────────────────────────────────────────
    //  Material-level animations
    //  These are shared across all layers of a Wc3Material and come
    //  from ParamBlock2 params on the Wc3Material itself (not on any
    //  specific bitmap).
    // ────────────────────────────────────────────────────────────────

    // KMTA — opacity animation (0..100 → 0..1)
    {
        Control* opacCtrl = opacityController(mtlRef);
        if (opacCtrl) {
            ir::InterpolationType interp = detectInterpFromController(opacCtrl);
            if (interp == ir::InterpolationType::Bezier &&
                isStepBezierController(opacCtrl))
                interp = ir::InterpolationType::None; // DontInterp round-trip
            else if (interp == ir::InterpolationType::None)
                interp = ir::InterpolationType::Linear;
            int32_t trackIdx = extractOpacityTrack(mtlRef, interp, model);
            if (trackIdx >= 0) {
                layer.alphaTrackIndex = trackIdx;
                // A keyed layer's static alpha is never shown: KMTA drives
                // alpha, and a sequence without keys falls back to 1, not to
                // the static value (mdx-m3-viewer sd.ts). An MDL layer has a
                // static Alpha or an Alpha track, never both, and 1.0 is what
                // Blizzard models carry, so write that rather than frame 0.
                layer.alpha = 1.0f;
                // Global sequence detection for the opacity track
                int32_t gsIdx = detectAndRegisterGlobalSeq(opacCtrl, model);
                if (gsIdx >= 0)
                    model.floatTracks[trackIdx].globalSequenceIndex = gsIdx;
            }
        }
    }

    // TXAN — UV animation (translation/rotation/scale) with dedup for
    // composite materials that share controllers across layers.
    {
        int32_t taIdx = extractTextureAnimationFromMaterial(mtlRef, model);
        if (taIdx >= 0)
            layer.textureAnimationIndex = taIdx;
    }

    // --- Reforged PBR maps ---
    //
    // IMPORTANT: Sub-texture ORDER must match ORIG Blizzard HD layout:
    //   [0] Diffuse, [1] Normal, [2] ORM, [3] Emissive,
    //   [4] TeamColor, [5] Environment
    // This order is used by the HD shader to bind textures to GPU samplers.
    // Writing them in a different order causes the shader to bind wrong
    // textures to wrong samplers → visual artifacts (wrong colors, broken
    // lighting). DO NOT reorder these blocks.
    // NeoDex names its maps normal_map / orm_map / emissive_map /
    // reflection_map with <kind>PrefixPath, keeps the MDX path its importer
    // read in orig<Kind>Path, and shares the diffuse's Wrap_Width/Height.
    auto addPbrMap = [&](const wchar_t* mapName, const wchar_t* prefixName,
                         const wchar_t* neoMapName, const wchar_t* neoPrefixName,
                         const wchar_t* neoOrigName, ir::TextureSlot slot) {
        Texmap* map = nullptr;
        const bool found = PBR::readTexmapByName(mtlRef, mapName, map) && map;
        if (!found && !(neoDex && PBR::readTexmapByName(mtlRef, neoMapName, map) && map))
            return;
        BitmapProperties bp = extractBitmapProperties(map);
        std::string path;
        if (found) {
            path = buildTexturePath(readMaterialPrefix(mtlRef, prefixName),
                                    extractBitmapFileName(map));
        } else {
            // orig<Kind>Path is an MDX path after an import, but a disk path
            // after a drag & drop onto the slot; only the former is usable.
            const std::string orig = readMaterialPrefix(mtlRef, neoOrigName);
            if (!orig.empty() && orig.find(':') == std::string::npos &&
                orig.rfind("\\\\", 0) != 0)
                path = orig;
            else
                path = buildTexturePath(readMaterialPrefix(mtlRef, neoPrefixName),
                                        extractBitmapFileName(map));
            BOOL wrap = FALSE;
            if (PBR::readBoolByName(mtlRef, L"Wrap_Width", t, wrap))  bp.wrapU = wrap != 0;
            wrap = FALSE;
            if (PBR::readBoolByName(mtlRef, L"Wrap_Height", t, wrap)) bp.wrapV = wrap != 0;
        }
        // A replaceable (tree, team colour, cliff) keeps an empty path in
        // every slot, like the diffuse: Blizzard's HD trees carry id 31-37
        // with no path in diffuse, normal and ORM alike.
        if (bp.replaceableId > 0) path.clear();
        ir::TextureRef ref;
        ref.textureIndex = findOrAddTexture(model, path, bp.replaceableId,
                                            bp.wrapU, bp.wrapV,
                                            extractBitmapDiskPath(map));
        ref.slot = slot;
        layer.textureRefs.push_back(ref);
    };

    addPbrMap(L"normalMap", L"normalPrefix", L"normal_map", L"normalPrefixPath",
              L"origNormalPath", ir::TextureSlot::Normal);
    addPbrMap(L"ormMap", L"ormPrefix", L"orm_map", L"ormPrefixPath",
              L"origOrmPath", ir::TextureSlot::ORM);
    addPbrMap(L"emissiveMap", L"emissivePrefix", L"emissive_map", L"emissivePrefixPath",
              L"origEmissivePath", ir::TextureSlot::Emissive);

    // HD TeamColor Mask (slot position 4 — must come AFTER Emissive, BEFORE Env).
    // teamColorMap is the authoritative carrier for HD Team Color masks.
    // When populated, we emit a TextureRef with slot=TeamColor, replaceableId=1,
    // and an empty path — matching ORIG Blizzard convention where HD Reforged
    // materials have a sub-texture at slot 4 referencing a TEXS entry with
    // replId=1 and empty path.
    //
    // Material-level replaceableId dropdown stays on "Not Used" for these HD
    // materials — the TC-Mask lives exclusively in the teamColorMap slot.
    {
        Texmap* tcMap = nullptr;
        if (PBR::readTexmapByName(mtlRef, L"teamColorMap", tcMap) && tcMap) {
            // The texture has no path, but its wrap flags are real TEXS data
            // and the importer keeps them on the bitmap in this slot.
            BitmapProperties tcProps = extractBitmapProperties(tcMap);
            ir::TextureRef tcRef;
            tcRef.textureIndex = findOrAddTexture(model,
                /*path=*/std::string(),
                /*replaceableId=*/1,  // Team Color
                tcProps.wrapU, tcProps.wrapV);
            tcRef.slot = ir::TextureSlot::TeamColor;
            layer.textureRefs.push_back(tcRef);

            // Debug log — trace when HD TC sub-texture is emitted
            {
                char tempPath[MAX_PATH];
                GetTempPathA(MAX_PATH, tempPath);
                std::string logPath = std::string(tempPath) + "mdlx_replaceable_debug.log";
                std::ofstream log(logPath, std::ios::app);
                if (log.is_open()) {
                    log << "  [TC-HD] teamColorMap slot populated"
                        << " -> emitted TextureRef(slot=TeamColor, replId=1, path='')"
                        << " textureIndex=" << tcRef.textureIndex
                        << "\n";
                }
            }
        } else if (neoDex) {
            // NeoDex marks the HD Team Color slot with teamColorTexId > 0.
            int tcId = 0;
            if (PBR::readIntByName(mtlRef, L"teamColorTexId", t, tcId) && tcId > 0) {
                BOOL wrapU = FALSE, wrapV = FALSE;
                PBR::readBoolByName(mtlRef, L"Wrap_Width", t, wrapU);
                PBR::readBoolByName(mtlRef, L"Wrap_Height", t, wrapV);
                ir::TextureRef tcRef;
                tcRef.textureIndex = findOrAddTexture(model, std::string(), 1,
                                                      wrapU != 0, wrapV != 0);
                tcRef.slot = ir::TextureSlot::TeamColor;
                layer.textureRefs.push_back(tcRef);
            }
        }
    }

    addPbrMap(L"environmentMap", L"environmentPrefix", L"reflection_map",
              L"reflectionPrefixPath", L"origReflectionPath",
              ir::TextureSlot::Environment);

    // Fresnel properties (emissiveGain / fresnelOpacity match NeoDex's
    // EmissiveGain / FresnelOpacity: names compare case-insensitively)
    PBR::readFloatByName(mtlRef, L"emissiveGain", t, layer.emissiveGain);
    PBR::readFloatByName(mtlRef, L"fresnelOpacity", t, layer.fresnelOpacity);
    readFloatFB(mtlRef, L"fresnelTeamCol", L"FresnelTeamColor", t, layer.fresnelTeamColor);

    // Fresnel color
    float fR = 1.0f, fG = 1.0f, fB = 1.0f;
    readFloatFB(mtlRef, L"fresnelR", L"FresnelColorR", t, fR);
    readFloatFB(mtlRef, L"fresnelG", L"FresnelColorG", t, fG);
    readFloatFB(mtlRef, L"fresnelB", L"FresnelColorB", t, fB);
    layer.fresnelColor = Point3(fR, fG, fB);

    // KMTE / KFC3 / KFCA / KFTC
    layer.emissiveGainTrackIndex     = extractMaterialFloatTrack(mtlRef, L"emissiveGain", model);
    layer.fresnelAlphaTrackIndex     = extractMaterialFloatTrack(mtlRef, L"fresnelOpacity", model);
    layer.fresnelTeamColorTrackIndex = extractMaterialFloatTrack(mtlRef, L"fresnelTeamCol", model);
    layer.fresnelColorTrackIndex     = extractFresnelColorTrack(mtlRef, layer.fresnelColor, model);

    return layer;
}

// A NeoDex material set to Team Color that also carries a texture stands for
// two MDX layers: the NeoDex importer folds an opaque Team Color layer and the
// texture drawn over it into one material, and NeoDex's Classic exporter
// splits it back (NeoDexSceneParser LoadLayers). Reforged binds the team
// colour through teamColorTexId instead, so the split is Classic only.
static bool isNeoDexTeamColorOverTexture(ReferenceTarget* mtlRef, int32_t formatVersion)
{
    if (formatVersion >= 900 || mtlRef->ClassID() != mdx_ids::NEODEX_MATERIAL)
        return false;
    int retexture = 1;
    if (!core::ParamBlockReader::readIntByName(mtlRef, L"retexture", 0, retexture) ||
        retexture != 2)
        return false;
    Texmap* tex = nullptr;
    return core::ParamBlockReader::readTexmapByName(mtlRef, L"texture", tex) && tex;
}

void extractWc3Material(ReferenceTarget* mtlRef, ir::IRModel& model,
                        core::ExportErrorReporter& /*reporter*/,
                        int32_t formatVersion)
{
    MaterialLevelProps matProps;
    ir::Material mat;

    if (isNeoDexTeamColorOverTexture(mtlRef, formatVersion)) {
        // Layer 0: the team colour, opaque and unshaded, as NeoDex writes it.
        ir::MaterialLayer tcLayer;
        tcLayer.blendMode = ir::BlendMode::None;
        tcLayer.unshaded = true;
        ir::TextureRef tcRef;
        tcRef.textureIndex = findOrAddTexture(model, std::string(), 1, false, false);
        tcRef.slot = ir::TextureSlot::Diffuse;
        tcLayer.textureRefs.push_back(tcRef);
        mat.layers.push_back(std::move(tcLayer));
        mat.layers.push_back(extractWc3Layer(mtlRef, model, matProps, true));
    } else {
        mat.layers.push_back(extractWc3Layer(mtlRef, model, matProps));
    }

    mat.priorityPlane = matProps.priorityPlane;
    mat.flags = matProps.flags;
    mat.shaderName = std::move(matProps.shaderName);
    model.materials.push_back(std::move(mat));
}

bool isWc3Composite(Mtl* mtl) {
    if (!mtl) return false;
    int n = mtl->NumSubMtls();
    if (n == 0) return false;
    bool hasWc3 = false;
    for (int i = 0; i < n; i++) {
        Mtl* sub = mtl->GetSubMtl(i);
        if (!sub) continue;
        if (sub->ClassID() != mdx_ids::WC3_MATERIAL &&
            sub->ClassID() != mdx_ids::NEODEX_MATERIAL)
            return false;
        hasWc3 = true;
    }
    return hasWc3;
}

void extractCompositeMaterial(Mtl* mtl, ir::IRModel& model,
                              core::ExportErrorReporter& /*reporter*/)
{
    ir::Material mat;
    MaterialLevelProps matProps;

    int n = mtl->NumSubMtls();
    for (int i = 0; i < n; i++) {
        Mtl* sub = mtl->GetSubMtl(i);
        if (!sub || (sub->ClassID() != mdx_ids::WC3_MATERIAL &&
                     sub->ClassID() != mdx_ids::NEODEX_MATERIAL)) continue;
        auto* ref = dynamic_cast<ReferenceTarget*>(sub);
        if (!ref) continue;

        MaterialLevelProps layerProps;
        ir::MaterialLayer layer = extractWc3Layer(ref, model, layerProps);
        mat.layers.push_back(std::move(layer));

        // Use properties from the first layer for the material
        if (mat.layers.size() == 1) {
            matProps = std::move(layerProps);
        }
    }

    mat.priorityPlane = matProps.priorityPlane;
    mat.flags = matProps.flags;
    mat.shaderName = std::move(matProps.shaderName);
    model.materials.push_back(std::move(mat));
}

// Material Fix's texHasAlpha: alpha below 250 somewhere on a 5 x 5 grid. An
// alpha channel that is opaque everywhere (common in BLP and TGA) does not
// count. False when the bitmap is not loaded, as in the tool.
static bool bitmapShowsAlpha(Texmap* tex) {
    BitmapTex* bmt = unwrapBitmapTex(tex);
    Bitmap* bm = bmt ? bmt->GetBitmap(0) : nullptr;
    if (!bm || bm->Width() <= 0 || bm->Height() <= 0) return false;
    const int w = bm->Width(), h = bm->Height();
    for (int iy = 0; iy <= 4; ++iy)
        for (int ix = 0; ix <= 4; ++ix) {
            BMM_Color_64 px;
            if (bm->GetPixels(ix * (w - 1) / 4, iy * (h - 1) / 4, 1, &px) &&
                px.a < 250 * 257)
                return true;
        }
    return false;
}

// A material that is not a Wc3 material - Standard, Physical, OpenPBR, ...,
// as old scenes keep them (NeoDex imports once left Physical materials on
// meshes). Exported the way Material Fix (MaterialFix.ms convertMaterial)
// converts it with the profile Fix all uses: one layer with its colour
// texture and opacity, and the profile's filter mode, flags, path prefix and
// wrapping - so the export is the same with or without Fix all. Always adds
// a material, textured or not: the caller has already handed its index out.
void extractForeignMaterial(Mtl* mtl, ir::IRModel& model) {
    const MaterialFixSettings fix = loadMaterialFixSettings();
    Texmap* colorTex = wdx::material::ColorTexmap(mtl);

    ir::Material mat;
    ir::MaterialLayer layer;
    int filterMode = fix.filterMode;
    if (fix.autoFilter)
        filterMode = (wdx::material::OpacityTexmap(mtl) || bitmapShowsAlpha(colorTex)) ? 2 : 1;
    layer.blendMode = blendModeFromFilterDropdown(filterMode);
    layer.alpha = mtl ? wdx::material::Opacity(mtl, 0) : 1.0f;
    layer.unshaded = fix.unshaded;
    layer.unfogged = fix.unfogged;
    layer.twoSided = fix.twoSided;
    layer.noDepthTest = fix.noDepthTest;
    layer.noDepthWrite = fix.noDepthSet && filterMode >= 3;
    if (fix.constantColor) mat.flags |= 0x01;

    if (colorTex) {
        const std::string fileName = extractBitmapPath(colorTex);
        if (!fileName.empty()) {
            bool wrapU = fix.uTile, wrapV = fix.vTile;       // WrapMode 2-5
            if (fix.wrapMode == 1)                           // keep the bitmap's
                readWrapFromBitmapTex(unwrapBitmapTex(colorTex), wrapU, wrapV);
            ir::TextureRef ref;
            ref.textureIndex = findOrAddTexture(
                model, buildTexturePath(wstrToUtf8(fix.prefixPath.c_str()), fileName), 0,
                wrapU, wrapV, extractBitmapDiskPath(colorTex));
            ref.slot = ir::TextureSlot::Diffuse;
            layer.textureRefs.push_back(ref);
        }
    }

    mat.layers.push_back(std::move(layer));
    model.materials.push_back(std::move(mat));
}

} // anonymous namespace

MaterialMap extractMaterials(const std::vector<core::SceneNode>& nodes,
                             ir::IRModel& model,
                             core::ExportErrorReporter& reporter,
                             int32_t formatVersion)
{
    // Reset the per-export dedup cache. The cache deduplicates TextureAnimation
    // entries across composite-material layers that share controllers.
    g_texAnimCache.clear();
    // Files may have been added since the last export.
    g_sceneTextures = {};

    // Banner the replaceable-debug log (trunc so each export starts fresh)
    {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logPath = std::string(tempPath) + "mdlx_replaceable_debug.log";
        std::ofstream log(logPath, std::ios::trunc);
        if (log.is_open()) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            log << "=== MDLX replaceableId Debug Log ===\n"
                << "Export started " << st.wYear << "-" << st.wMonth << "-" << st.wDay
                << " " << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "\n"
                << "----------------------------------------\n";
        }
    }

    std::unordered_map<Mtl*, int32_t> mtlToIndex;

    // ── Helper: run the appropriate extractor for a Max material ─────
    // Dispatches by ClassID (Wc3Material → Wc3 extractor; Wc3Composite →
    // composite extractor; anything else → extractForeignMaterial). Called from
    // both the mesh-material pass below and the ribbon-material pass
    // after it, so the logic stays in sync.
    auto addMaterial = [&](Mtl* mtl) -> int32_t {
        if (!mtl) return -1;
        auto existing = mtlToIndex.find(mtl);
        if (existing != mtlToIndex.end()) return existing->second;

        int32_t idx = static_cast<int32_t>(model.materials.size());
        auto* ref = dynamic_cast<ReferenceTarget*>(mtl);
        if (ref && (mtl->ClassID() == mdx_ids::WC3_MATERIAL ||
                    mtl->ClassID() == mdx_ids::NEODEX_MATERIAL)) {
            extractWc3Material(ref, model, reporter, formatVersion);
        } else if (isWc3Composite(mtl)) {
            extractCompositeMaterial(mtl, model, reporter);
        } else {
            extractForeignMaterial(mtl, model);
        }
        mtlToIndex[mtl] = idx;
        return idx;
    };

    // ── Pass 1: mesh materials (unchanged behavior) ───────────────────
    for (auto& sn : nodes) {
        if (sn.category != core::NodeCategory::Mesh) continue;
        if (!sn.maxNode) continue;

        Mtl* mtl = sn.maxNode->GetMtl();
        if (!mtl) continue;
        addMaterial(mtl);
    }

    // ── Pass 2: ribbon-emitter materials ──────────────────────────────
    //
    // Wc3Ribbon stores its material reference in the paramblock
    // (pb_material = 7), NOT on the INode — so INode::GetMtl() returns
    // nullptr for a ribbon node and the mesh-pass above skips them.
    // Without this pass, the ribbon extractor's mtlToIndex.find() fails
    // and every ribbon ends up with materialIndex=-1, which the builder
    // writes as materialId=0 (usually the body-mesh material) — the
    // ribbon then renders with the wrong texture/blend mode ingame.
    //
    // Known symptom: Saurus warrior's Tail_Ribbon / Club_Ribbon exports
    // with materialId=0 instead of the intended materialId=4, losing
    // RibbonBlur1.blp and the additive blend mode.
    constexpr ParamID kRibbonMaterialParamID = 7;  // pb_material in Ribbon.h
    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3Ribbon") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        Object* baseObj = obj;
        while (baseObj && baseObj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
            baseObj = static_cast<IDerivedObject*>(baseObj)->GetObjRef();

        Mtl* mtl = nullptr;
        if (baseObj && baseObj->ClassID() == mdx_ids::NEODEX_RIBBON) {
            // NeoDex BlizRibbon: the material param is "rmaterial" — the same
            // lookup wc3_ribbon_extractor does, which must find it here.
            IParamBlock2* pb = nullptr;
            ParamID pid = 0;
            if (findAnimParam(baseObj, L"rmaterial", pb, pid) &&
                pb->GetParamDef(pid).type == TYPE_MTL)
                mtl = pb->GetMtl(pid, 0);
        } else if (IParamBlock2* pb = core::ParamBlockReader::findParamBlock(ref, 0)) {
            mtl = pb->GetMtl(kRibbonMaterialParamID, 0);
        }
        if (!mtl) continue;
        addMaterial(mtl);
    }

    // Assign material indices to meshes
    for (auto& mesh : model.meshes) {
        if (mesh.nodeIndex < 0 || mesh.nodeIndex >= static_cast<int32_t>(model.nodes.size()))
            continue;
        auto* maxNode = model.nodes[mesh.nodeIndex].maxNode;
        if (!maxNode) continue;
        Mtl* mtl = maxNode->GetMtl();
        if (!mtl) continue;

        auto it = mtlToIndex.find(mtl);
        if (it != mtlToIndex.end())
            mesh.materialIndex = it->second;
    }

    return mtlToIndex;
}

} // namespace mdx_extract
