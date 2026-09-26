// MDLXImporter — Wc3MaterialBuilder implementation
#include "wc3_material_builder.h"
#include "texture_resolver.h"
#include "../mdlx_class_ids.h"

#include <scene/paramblock_reader.h>
#include <iparamb2.h>
#include <stdmat.h>
#include <bitmap.h>
#include <plugapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

// Debug log shared with mdlx_importer_plugin.cpp
static std::ofstream& matLog() {
    static std::ofstream s_log;
    if (!s_log.is_open()) {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring p(tmp);
        p += L"mdlx_import_debug.log";
        s_log.open(p, std::ios::app);
    }
    return s_log;
}
#define MLOG matLog()

namespace {

using PBR = core::ParamBlockReader;

// ── Param write helper (same as scene builders) ─────────────

struct PBParam {
    IParamBlock2* pb = nullptr;
    ParamID id = -1;
    explicit operator bool() const { return pb != nullptr; }
};

PBParam findParam(ReferenceTarget* target, const wchar_t* name) {
    if (!target) return {};
    for (int i = 0; i < target->NumRefs(); i++) {
        auto* ref = target->GetReference(i);
        auto* pb = dynamic_cast<IParamBlock2*>(ref);
        if (!pb) continue;
        auto* desc = pb->GetDesc();
        if (!desc) continue;
        for (int j = 0; j < desc->Count(); j++) {
            ParamID pid = desc->IndextoID(j);
            const ParamDef& pd = desc->GetParamDef(pid);
            if (pd.int_name && _wcsicmp(pd.int_name, name) == 0)
                return { pb, pid };
        }
    }
    return {};
}

void pbSetInt(ReferenceTarget* t, const wchar_t* n, int v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetFloat(ReferenceTarget* t, const wchar_t* n, float v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetBool(ReferenceTarget* t, const wchar_t* n, BOOL v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetString(ReferenceTarget* t, const wchar_t* n, const MCHAR* v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetTexmap(ReferenceTarget* t, const wchar_t* n, Texmap* v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}

std::wstring toWstr(const std::string& u8) {
    if (u8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring r(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, r.data(), len);
    return r;
}

// Narrow a wide string for the debug log, ASCII only.
// std::filesystem::path::string() must NEVER be used for this: it uses the
// STL's strict wide→narrow converter, which throws std::system_error ("No
// mapping for the Unicode character exists in the target multi-byte code
// page") for any character the ANSI code page cannot represent — e.g. a
// model imported from a folder with a CJK name. Nothing in the import path
// catches that, so it escaped DoImport and Max reported an unexpected
// system exception. Same helper as texture_resolver.cpp's wlog().
std::string wlog(const std::wstring& ws) {
    std::string s;
    s.reserve(ws.size());
    for (wchar_t c : ws)
        s += (c < 128) ? static_cast<char>(c) : '?';
    return s;
}

// Narrow a wide path to UTF-8 for writing into an .ifl file.
// Verified against Max 2027 (see the IFL writer below for what was tested):
// Max decodes IFL entries as UTF-8, so this round-trips a path under a
// non-ANSI directory. For an all-ASCII path the bytes are unchanged.
std::string toUtf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    const int n = static_cast<int>(ws.size());
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), n, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), n, out.data(), len, nullptr, nullptr);
    return out;
}

} // anonymous namespace


namespace {


/// Create a native 3ds Max BitmapTex for one IR texture entry.
/// Sets file path and tiling (wrapU/wrapV) on the StdUVGen.
/// replaceableId is NOT set here — it is stored on the Wc3Material later
/// (see buildSingleLayerWc3Material).
/// sphereEnvMap is also set on the Wc3Material level via StdUVGen.
/// Standard Warcraft III ReplaceableID → texture path mapping.
/// MDX models with replaceableId > 0 often have an empty filePath because the
/// game engine resolves the path at render time. We construct the canonical
/// path so the importer can hand the renderer (or CASC/MPQ extractor) a real
/// file to load. Returns empty string for unknown IDs.
static std::wstring replaceableIdToPath(int32_t id) {
    switch (id) {
    case 1:  return L"ReplaceableTextures\\TeamColor\\TeamColor00.blp";
    case 2:  return L"ReplaceableTextures\\TeamGlow\\TeamGlow00.blp";
    case 11: return L"ReplaceableTextures\\Cliff\\Cliff0.blp";
    case 21: return L"ReplaceableTextures\\LordaeronTree\\LordaeronSummerTree.blp";
    case 22: return L"ReplaceableTextures\\AshenvaleTree\\AshenTree.blp";
    case 23: return L"ReplaceableTextures\\BarrensTree\\BarrensTree.blp";
    case 24: return L"ReplaceableTextures\\NorthrendTree\\NorthTree.blp";
    case 25: return L"ReplaceableTextures\\Mushroom\\MushroomTree.blp";
    case 31: return L"ReplaceableTextures\\RuinsTree\\RuinsTree.blp";
    case 32: return L"ReplaceableTextures\\OutlandMushroomTree\\MushroomTree.blp";
    default: return {};
    }
}

/// Local logging helper (the file-scope `wlog` lives in a different anonymous
/// namespace and isn't visible from here). Narrows a wide string for ostream
/// output; non-ASCII becomes '?'.
static std::string wlog_local(const std::wstring& ws) {
    std::string s; s.reserve(ws.size());
    for (wchar_t c : ws) s += (c < 128) ? static_cast<char>(c) : '?';
    return s;
}

/// Unwrap a Texmap to its underlying native BitmapTex. Returns tex itself
/// when it's a BitmapTex; for a Wc3Bitmap wrapper, returns its BitmapTex
/// delegate; otherwise nullptr.
static BitmapTex* unwrapToBitmapTex(Texmap* tex) {
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

Texmap* createNativeBitmap(const ir::Texture& irTex, const std::wstring& modelDir,
                           mdx_scene::TextureResolver* resolver, Interface* gi)
{
    MLOG << "[TEX] createNativeBitmap for '" << irTex.filePath << "'"
         << " replaceableId=" << irTex.replaceableId << std::endl;

    // Preferred: a Wc3Bitmap scripted texture wrapping a BitmapTex delegate.
    // The Wc3Bitmap carries the WC3 texture metadata (wrapU/wrapV,
    // replaceableId) and — since the TXAN refactor — the UV animation params
    // (anim_UOffset & co.) that the exporter reads back. Falls back to a
    // plain BitmapTex when the scripted plugin isn't installed.
    Texmap* resultTex = nullptr;
    BitmapTex* bmpTex = nullptr;
    if (gi) {
        auto* wc3Bmp = static_cast<Texmap*>(
            gi->CreateInstance(TEXMAP_CLASS_ID, mdx_ids::WC3_BITMAP));
        if (wc3Bmp) {
            bmpTex = unwrapToBitmapTex(wc3Bmp);
            if (bmpTex) {
                resultTex = wc3Bmp;
            } else {
                MLOG << "[TEX]   Wc3Bitmap created but no BitmapTex delegate found"
                     << " - falling back to plain BitmapTex" << std::endl;
            }
        }
    }
    if (!resultTex) {
        bmpTex = NewDefaultBitmapTex();
        if (!bmpTex) {
            MLOG << "[TEX]   ERROR: NewDefaultBitmapTex() returned null" << std::endl;
            return nullptr;
        }
        resultTex = bmpTex;
        MLOG << "[TEX]   using plain BitmapTex (Wc3Bitmap plugin unavailable)"
             << std::endl;
    }

    // Decide which path to resolve. Priority:
    //   1. Explicit filePath from the MDX (covers most cases).
    //   2. ReplaceableID-derived canonical path when filePath is empty
    //      (e.g. v800 TeamColor / TeamGlow / Cliff / Tree textures that the
    //      engine fills in at render time).
    std::wstring relPath;
    if (!irTex.filePath.empty()) {
        relPath = toWstr(irTex.filePath);
    } else if (irTex.replaceableId > 0) {
        relPath = replaceableIdToPath(irTex.replaceableId);
        if (!relPath.empty()) {
            MLOG << "[TEX]   replaceableId=" << irTex.replaceableId
                 << " -> derived path '" << wlog_local(relPath) << "'" << std::endl;
        } else {
            MLOG << "[TEX]   replaceableId=" << irTex.replaceableId
                 << " has no canonical path mapping - leaving slot empty" << std::endl;
        }
    }

    if (!relPath.empty()) {
        std::wstring wpath = resolver ? resolver->Resolve(relPath)
                                       : mdx_scene::resolveTexturePath(modelDir, relPath);
        bmpTex->SetMapName(wpath.c_str());
        MLOG << "[TEX]   SetMapName '" << wlog_local(wpath) << "'" << std::endl;
    }

    // Configure tiling (wrap) on the StdUVGen
    // U_WRAP and V_WRAP are the standard 3ds Max texture symmetry flags
    StdUVGen* uvGen = bmpTex->GetUVGen();
    if (uvGen) {
        int tilingFlags = 0;
        if (irTex.wrapU) tilingFlags |= U_WRAP;
        if (irTex.wrapV) tilingFlags |= V_WRAP;
        uvGen->SetTextureTiling(tilingFlags);

        MLOG << "[TEX]   Tiling: U=" << (irTex.wrapU ? "wrap" : "clamp")
             << " V=" << (irTex.wrapV ? "wrap" : "clamp") << std::endl;
    }

    // Mirror the WC3 texture metadata onto the Wc3Bitmap params so the UI
    // checkboxes / dropdown reflect reality. (The scripted on-set handlers
    // don't fire for C++ SetValue, which is fine — the delegate's UVGen was
    // already configured above.) The dropdown is 1-based: 1 = Not Used.
    if (resultTex != bmpTex) {
        auto* wc3Ref = dynamic_cast<ReferenceTarget*>(resultTex);
        if (wc3Ref) {
            pbSetBool(wc3Ref, L"wrapU", irTex.wrapU ? TRUE : FALSE);
            pbSetBool(wc3Ref, L"wrapV", irTex.wrapV ? TRUE : FALSE);
            pbSetInt(wc3Ref, L"replaceableId", irTex.replaceableId + 1);
        }
    }

    return resultTex;
}

// Map ir::BlendMode to filterMode int (1-based)
int blendModeToFilterMode(ir::BlendMode bm) {
    switch (bm) {
    case ir::BlendMode::None:        return 1;
    case ir::BlendMode::Transparent: return 2;
    case ir::BlendMode::Blend:       return 3;
    case ir::BlendMode::Additive:   return 4;
    case ir::BlendMode::AddAlpha:   return 5;
    case ir::BlendMode::Modulate:   return 6;
    case ir::BlendMode::Modulate2x: return 7;
    default: return 1;
    }
}

// Map ir::TextureSlot to Wc3Material param name
const wchar_t* textureSlotParamName(ir::TextureSlot slot) {
    switch (slot) {
    case ir::TextureSlot::Diffuse:     return L"diffuseMap";
    case ir::TextureSlot::Normal:      return L"normalMap";
    case ir::TextureSlot::ORM:         return L"ormMap";
    case ir::TextureSlot::Emissive:    return L"emissiveMap";
    case ir::TextureSlot::TeamColor:   return L"teamColorMap";
    case ir::TextureSlot::Reflection:
    case ir::TextureSlot::Environment: return L"environmentMap";
    default: return nullptr;
    }
}

// Map ir::TextureSlot to Wc3Material prefix-param name.
// Must stay in sync with the Wc3Material.ms parameter declarations.
const wchar_t* textureSlotPrefixParamName(ir::TextureSlot slot) {
    switch (slot) {
    case ir::TextureSlot::Diffuse:     return L"diffusePrefix";
    case ir::TextureSlot::Normal:      return L"normalPrefix";
    case ir::TextureSlot::ORM:         return L"ormPrefix";
    case ir::TextureSlot::Emissive:    return L"emissivePrefix";
    case ir::TextureSlot::TeamColor:   return L"teamColorPrefix";
    case ir::TextureSlot::Reflection:
    case ir::TextureSlot::Environment: return L"environmentPrefix";
    default: return nullptr;
    }
}

// Extract the folder-prefix portion of an MDX texture path.
// "Textures\Foo.blp"              → "Textures\"
// "war3mapImported\bar.blp"       → "war3mapImported\"
// "ReplaceableTextures\Team\x.blp"→ "ReplaceableTextures\Team\"
// "foo.blp"                       → ""
// Handles both separators; normalizes forward slash to backslash.
std::wstring extractTexturePrefix(const std::string& mdxPath) {
    if (mdxPath.empty()) return {};
    std::wstring w(mdxPath.begin(), mdxPath.end());
    std::replace(w.begin(), w.end(), L'/', L'\\');
    auto lastSep = w.find_last_of(L'\\');
    if (lastSep == std::wstring::npos) return {};
    return w.substr(0, lastSep + 1);  // include trailing separator
}

// ── IFL (Image File List) generator for KMTF texture animation ──
// When a material layer has a textureIdTrack (KMTF in MDX), the game
// cycles through multiple textures at specified time intervals.
// 3ds Max has a native equivalent: BitmapTex references an .ifl file
// (simple filename-per-line list), and three additional properties
// control timing:
//   - SetStartTime(t)       : when to begin cycling
//   - SetPlaybackRate(r)    : frames-of-IFL per render-frame
//                             (e.g. 0.5 → hold each frame for 2 render frames)
//   - SetEndCondition(c)    : END_LOOP (0) or END_HOLD (2)
// Approach is modelled after the NeoDex MaxScript importer: the IFL
// contains ONLY filenames (no per-line frame counts), and timing is
// driven by the playback rate derived from the average key interval.

struct IFLGenResult {
    std::wstring iflPath;
    TimeValue    startTime;
    float        playbackRate;
    int          endCondition;
};

IFLGenResult generateIFLForLayer(
    const ir::MaterialLayer& layer,
    const ir::IRModel& irModel,
    const std::wstring& modelDir,
    mdx_scene::TextureResolver* resolver = nullptr)
{
    IFLGenResult result{L"", 0, 1.0f, 0};

    int32_t tidx = layer.textureIdTrackIndex;
    MLOG << "[IFL-DEBUG] generateIFLForLayer: tidx=" << tidx << std::endl;

    if (tidx < 0) {
        MLOG << "[IFL-DEBUG] no textureIdTrack — skip" << std::endl;
        return result;
    }
    if (tidx >= static_cast<int32_t>(irModel.intTracks.size())) {
        MLOG << "[IFL-DEBUG] tidx out of bounds (intTracks.size=" 
             << irModel.intTracks.size() << ")" << std::endl;
        return result;
    }

    const auto& track = irModel.intTracks[tidx];
    MLOG << "[IFL-DEBUG] track has " << track.keys.size() << " keys, "
         << "globalSeq=" << track.globalSequenceIndex << std::endl;

    if (track.keys.size() < 2) {
        MLOG << "[IFL-DEBUG] <2 keys — skip" << std::endl;
        return result;
    }

    // Log the actual keys.
    for (size_t i = 0; i < track.keys.size(); ++i) {
        MLOG << "[IFL-DEBUG]   key[" << i << "] t=" << track.keys[i].time
             << " val=" << track.keys[i].value << std::endl;
    }

    // Ensure we have at least two different texture indices.
    int32_t firstVal = track.keys[0].value;
    bool hasVariation = false;
    for (const auto& k : track.keys) {
        if (k.value != firstVal) { hasVariation = true; break; }
    }
    if (!hasVariation) {
        MLOG << "[IFL-DEBUG] all keys same value — skip" << std::endl;
        return result;
    }

    // Build IFL content: one FULL PATH per line (this is what NeoDex
    // does — Max's IFL loader resolves relative or absolute paths).
    std::stringstream iflA;
    int linesWritten = 0;
    for (const auto& k : track.keys) {
        int32_t texIdx = k.value;
        if (texIdx < 0 || texIdx >= static_cast<int32_t>(irModel.textures.size())) {
            MLOG << "[IFL-DEBUG]   skip bad texIdx " << texIdx << std::endl;
            continue;
        }
        const auto& irTex = irModel.textures[texIdx];
        // Use the same path resolution as createNativeBitmap so the
        // IFL and the non-animated fallback point at consistent files.
        std::wstring rel = toWstr(irTex.filePath);
        std::wstring fullPath = resolver ? resolver->Resolve(rel)
                                          : mdx_scene::resolveTexturePath(modelDir, rel);

        // Write the ABSOLUTE path, encoded as UTF-8. Tested against Max 2027
        // with a bitmap under a CJK directory:
        //   absolute + UTF-8            -> loads
        //   absolute + UTF-8 with BOM   -> loads
        //   absolute + ANSI code page   -> fails ('?' substitutions)
        //   relative to the IFL's dir   -> fails; Max resolves a relative
        //                                  entry against the configured
        //                                  External Files paths, NOT against
        //                                  the IFL's own location
        // For an all-ASCII path this is byte-for-byte what we wrote before.
        //
        // This was originally a char-by-char wchar_t->char truncation, which
        // turned every non-ASCII path character into a stray byte — U+6E0A
        // ('渊') became 0x0A, splitting the entry across two lines.
        const std::string narrow = toUtf8(fullPath);
        iflA << narrow << "\n";
        ++linesWritten;

        MLOG << "[IFL-DEBUG]   wrote '" << narrow << "'" << std::endl;
    }

    if (linesWritten == 0) {
        MLOG << "[IFL-DEBUG] no valid texture entries — skip" << std::endl;
        return result;
    }

    // Compute average inter-key interval (in ticks). NeoDex formula:
    //   playbackRate = TicksPerFrame / avgInterval_ticks
    const TimeValue tpf = GetTicksPerFrame();
    const size_t n = track.keys.size();
    TimeValue avgIntervalTicks = tpf;
    if (n >= 2) {
        TimeValue total = track.keys[n - 1].time - track.keys[0].time;
        avgIntervalTicks = total / static_cast<TimeValue>(n - 1);
        if (avgIntervalTicks < 1) avgIntervalTicks = tpf;
    }
    float playbackRate = static_cast<float>(tpf) /
                         static_cast<float>(avgIntervalTicks);

    // End condition: bound to global sequence → loop (0), else hold (2).
    int endCondition = (track.globalSequenceIndex >= 0) ? 0 : 2;

    TimeValue startTime = track.keys[0].time;

    // Write IFL file next to the model.
    std::stringstream iflName;
    iflName << "anim_t" << tidx << ".ifl";
    namespace fs = std::filesystem;
    fs::path iflPath = fs::path(modelDir) / iflName.str();

    MLOG << "[IFL-DEBUG] writing IFL file to: " << wlog(iflPath.wstring()) << std::endl;

    std::ofstream ofs(iflPath, std::ios::binary);
    if (!ofs) {
        MLOG << "[IFL] Failed to open for write: " << wlog(iflPath.wstring()) << std::endl;
        return result;
    }
    std::string content = iflA.str();
    ofs.write(content.data(), content.size());
    ofs.close();

    MLOG << "[IFL] Generated '" << iflName.str() << "' — "
         << linesWritten << " filenames, startTime=" << startTime
         << " playbackRate=" << playbackRate
         << " endCondition=" << endCondition
         << " avgInterval=" << avgIntervalTicks << " ticks" << std::endl;
    MLOG << "[IFL] Content:\n" << content << std::endl;

    result.iflPath      = iflPath.wstring();
    result.startTime    = startTime;
    result.playbackRate = playbackRate;
    result.endCondition = endCondition;
    return result;
}

} // anonymous namespace

namespace mdx_scene {

bool isHdLayer(const ir::Material& irMat, const ir::MaterialLayer& layer) {
    if (!irMat.shaderName.empty())
        return true;
    for (const auto& texRef : layer.textureRefs) {
        if (texRef.slot != ir::TextureSlot::Diffuse &&
            texRef.slot != ir::TextureSlot::TeamColor)
            return true;
    }
    return false;
}

// ── Helper: Build a single Wc3Material from one layer ────────

static Mtl* buildSingleLayerWc3Material(
    const ir::MaterialLayer& layer,
    const ir::Material& irMat,
    const ir::IRModel& irModel,
    const std::vector<Texmap*>& texmaps,
    const std::wstring& modelDir,
    mdx_scene::TextureResolver* resolver,
    Interface* gi,
    bool showInViewport = true)
{
    Mtl* mtl = static_cast<Mtl*>(
        gi->CreateInstance(MATERIAL_CLASS_ID, mdx_ids::WC3_MATERIAL));
    if (!mtl) return nullptr;

    auto* ref = dynamic_cast<ReferenceTarget*>(mtl);
    if (!ref) return mtl;

    // Shader dropdown: 1 SD, 2 HD, 3 SD on HD, 4 Crystal. The layer's MDX
    // shader decides; the texture-slot heuristic only covers a shader the
    // dropdown has no entry for.
    int shaderDropdown = 0;
    switch (layer.shaderType) {
    case 0:  shaderDropdown = 1; break;  // SD
    case 1:  shaderDropdown = 2; break;  // HD
    case 2:  shaderDropdown = 3; break;  // SD on HD
    case 24: shaderDropdown = 4; break;  // Crystal
    default: shaderDropdown = isHdLayer(irMat, layer) ? 2 : 1; break;
    }
    // The parser only merges a v900/v1000 Shader_HD_DefaultUnit material into
    // an HD layer when it has all six layers; any other shape keeps SD. The
    // shader name (or a PBR map) still says HD, as it did before the shader
    // was carried through.
    if (shaderDropdown == 1 && isHdLayer(irMat, layer))
        shaderDropdown = 2;
    pbSetInt(ref, L"shaderType", shaderDropdown);

    // Filter mode (1-based)
    pbSetInt(ref, L"filterMode", blendModeToFilterMode(layer.blendMode));

    // Opacity: Wc3Material uses 0-100, Standard delegate uses 0-1
    pbSetFloat(ref, L"opacity", layer.alpha * 100.0f);
    // Sync to Standard material delegate (on set handler may not fire from C++)
    for (int ri = 0; ri < mtl->NumRefs(); ri++) {
        auto* r = mtl->GetReference(ri);
        if (auto* sm = dynamic_cast<StdMat2*>(r)) {
            sm->SetOpacity(layer.alpha, 0);
            break;
        }
    }

    // Shading flags
    pbSetBool(ref, L"twoSided", layer.twoSided ? TRUE : FALSE);
    pbSetBool(ref, L"unshaded", layer.unshaded ? TRUE : FALSE);
    pbSetBool(ref, L"unfogged", layer.unfogged ? TRUE : FALSE);
    pbSetBool(ref, L"noDepthTest", layer.noDepthTest ? TRUE : FALSE);
    pbSetBool(ref, L"noDepthSet", layer.noDepthWrite ? TRUE : FALSE);
    pbSetBool(ref, L"ambientOcclusion", layer.ambientOcclusion ? TRUE : FALSE);
    pbSetBool(ref, L"backFacesForShadows", layer.backFacesForShadows ? TRUE : FALSE);

    // Priority plane
    pbSetInt(ref, L"priorityPlane", irMat.priorityPlane);

    // Coord ID
    pbSetInt(ref, L"coordId", layer.uvSetIndex);

    // Material flags
    pbSetBool(ref, L"constantColor", (irMat.flags & 0x01) ? TRUE : FALSE);
    pbSetBool(ref, L"fullResolution", (irMat.flags & 0x20) ? TRUE : FALSE);

    // Sort order: 0x08 = nearToFar(2), 0x10 = farToNear(3), else 1
    int sortOrder = 1;
    if (irMat.flags & 0x08) sortOrder = 2;
    else if (irMat.flags & 0x10) sortOrder = 3;
    pbSetInt(ref, L"sortOrder", sortOrder);

    // Shader path (Reforged)
    if (!irMat.shaderName.empty()) {
        auto wshader = toWstr(irMat.shaderName);
        pbSetString(ref, L"shaderPath", wshader.c_str());
    }

    // ── Material-level replaceableId dropdown ──
    //
    // MDX semantics: every texture in the TEXS chunk can carry a replaceableId
    // (0 = normal, 1 = TeamColor, 2 = TeamGlow, 3+ = Cliff/Tree/etc.).
    // The Wc3Material plug-in has two DIFFERENT constructs for team colour:
    //
    //   Construct A — a pure TC material (SD/classic):
    //     ddReplaceable = "Team Color"      (dropdown)
    //     diffuseMap = NONE
    //     -> the whole material becomes the TC surface
    //
    //   Construct B — an HD material with a TC mask (Reforged):
    //     ddReplaceable = "Not Used"        (the dropdown stays empty)
    //     diffuseMap   = main_diffuse.dds
    //     teamColorMap = TC_mask.dds        (a dedicated slot)
    //     -> the mask decides where the TC tint is applied
    //
    // The discriminator is the SLOT of the texture reference, not the
    // replaceableId on its own.
    //   slot == Diffuse with replaceableId > 0 -> construct A, set the dropdown
    //   slot == TeamColor (an HD sub-texture)  -> construct B; the teamColorMap
    //                                             slot is set below in the
    //                                             texmap loop and the dropdown
    //                                             stays 0
    //
    // Historical bug: an earlier attempt took max(replaceableId) across all
    // texture refs. That worked for SD but ruined HD materials: Arthas v1200
    // has Diffuse(replId=0) + a TC sub-texture(replId=1), so max=1 put the
    // dropdown wrongly on "Team Color" and the exporter then dropped the
    // diffuse path. NeoDex's equivalent is NeoDexSceneRebuilder.ms lines
    // 1068-1073 (teamColorTexId separately for HD, layer.retexture only for the
    // SD diffuse).
    //
    // Dropdown mapping (Wc3Material.ms line 411):
    //   1 = Not Used (=MDX 0)
    //   2 = Team Color (=MDX 1)
    //   3 = Team Glow (=MDX 2)
    //   4+ = Cliff, Lord Cliffington etc. (=MDX 3+)
    {
        int32_t diffuseReplaceableId = 0;
        for (const auto& texRef : layer.textureRefs) {
            if (texRef.slot != ir::TextureSlot::Diffuse) continue;
            if (texRef.textureIndex < 0 ||
                texRef.textureIndex >= static_cast<int32_t>(irModel.textures.size()))
                continue;
            int32_t rid = irModel.textures[texRef.textureIndex].replaceableId;
            if (rid > diffuseReplaceableId) diffuseReplaceableId = rid;
        }
        pbSetInt(ref, L"replaceableId", diffuseReplaceableId + 1);
    }

    // Texture maps — use pre-created Wc3Bitmaps (1 per MDX TEXS entry)
    {
        Texmap* diffuseTexmap = nullptr;

        for (const auto& texRef : layer.textureRefs) {
            if (texRef.textureIndex < 0 ||
                texRef.textureIndex >= static_cast<int32_t>(texmaps.size()))
                continue;

            Texmap* texmap = texmaps[texRef.textureIndex];
            if (!texmap) continue;

            const wchar_t* paramName = textureSlotParamName(texRef.slot);
            if (!paramName) continue;

            pbSetTexmap(ref, paramName, texmap);

            // Route the bitmap at the UV set this layer names. MDX coordId is a
            // 0-based UVAS index and Max map channels are 1-based; the importer
            // writes MDX set 0 -> channel 1 and set 1 -> channel 2, so the
            // channel is coordId + 1. Without this every BitmapTex kept its
            // default channel 1 and a layer with coordId 1 silently sampled the
            // first unwrap — the tiling call below was the only UVGen setup.
            // coordId -1 is SphereEnvMap, whose UVs are generated rather than
            // fetched, so leave that one alone.
            //
            // Texmaps are shared per TEXS entry, so a texture two layers use
            // with DIFFERENT coordIds takes whichever is assigned last. Shipping
            // MDX content does not do that, and splitting the bitmap per layer
            // would break the texture-animation clone cache built above.
            //
            // This does not carry Reforged HD's baked AO: `hd_ps` reads ORM.x at
            // UV1 while ORM.yzw stay at UV0, and one BitmapTex has one channel.
            // The preview gets the second unwrap straight from map channel 2 in
            // MaxSceneAdapter::GetMeshes(), which is where it is actually used.
            if (layer.uvSetIndex >= 0) {
                if (BitmapTex* slotBmp = unwrapToBitmapTex(texmap)) {
                    if (StdUVGen* slotUv = slotBmp->GetUVGen())
                        slotUv->SetMapChannel(layer.uvSetIndex + 1);
                }
            }

            // Store the MDX folder prefix (e.g. "Textures\") on the material
            // so the exporter can reconstruct the original full path on
            // re-export. The user can also edit this in the material UI to
            // relocate textures (e.g. change "Textures\" → "war3mapImported\").
            if (texRef.textureIndex < static_cast<int32_t>(irModel.textures.size())) {
                const wchar_t* prefixParam = textureSlotPrefixParamName(texRef.slot);
                if (prefixParam) {
                    const auto& irTex = irModel.textures[texRef.textureIndex];
                    std::wstring prefix = extractTexturePrefix(irTex.filePath);
                    pbSetString(ref, prefixParam, prefix.c_str());
                }
            }

            if (texRef.slot == ir::TextureSlot::Diffuse) {
                diffuseTexmap = texmap;

                // ── sphereEnvMap: set via native BitmapTex StdUVGen mapping type ──
                // (unwraps the Wc3Bitmap wrapper to its BitmapTex delegate)
                if (layer.sphereEnvMap) {
                    if (BitmapTex* bmpTex = unwrapToBitmapTex(texmap)) {
                        StdUVGen* uvGen = bmpTex->GetUVGen();
                        if (uvGen) {
                            uvGen->SetCoordMapping(UVMAP_SPHERE_ENV);
                            MLOG << "[MAT]   sphereEnvMap -> UVMAP_SPHERE_ENV on diffuse" << std::endl;
                        }
                    }
                }
            }
        }

        if (diffuseTexmap && showInViewport) {
            diffuseTexmap->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED);
            // Nitrous displays the scripted wrapper via its delegate — flag
            // the delegate too so "Show Shaded Material in Viewport" works.
            if (BitmapTex* d = unwrapToBitmapTex(diffuseTexmap))
                if (static_cast<Texmap*>(d) != diffuseTexmap)
                    d->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED);
            mtl->SetActiveTexmap(diffuseTexmap);
        }

        // Sync the diffuse to the StdMat2 delegate. Wc3Material is a scripted
        // plugin extending StdMat2; its `on diffuseMap set val do` handler
        // sets `delegate.diffuseMap = getNativeTex val` so the viewport (which
        // renders the delegate, not the script) and the mbDiffuse button text
        // both reflect the assignment. That handler does NOT fire when C++
        // calls IParamBlock2::SetValue — same quirk we already work around
        // above for opacity. Find the StdMat2 delegate among the references
        // and update its diffuse subtexmap directly so the diffuse actually
        // renders + the UI shows the filename instead of "(None)".
        if (diffuseTexmap) {
            for (int ri = 0; ri < mtl->NumRefs(); ri++) {
                auto* r = mtl->GetReference(ri);
                if (auto* sm = dynamic_cast<StdMat2*>(r)) {
                    sm->SetSubTexmap(ID_DI, diffuseTexmap);
                    sm->EnableMap(ID_DI, TRUE);
                    break;
                }
            }
        }

        // ── IFL texture animation (KMTF track in MDX) ──
        // Following NeoDex's proven approach: generate an .ifl file with
        // a simple filename-per-line list, then use BitmapTex's native
        // playback controls to drive timing.
        MLOG << "[IFL-CHECK] layer.textureIdTrackIndex=" << layer.textureIdTrackIndex
             << " diffuseTexmap=" << (diffuseTexmap ? "yes" : "null");
        BitmapTex* diffuseBmpTex = unwrapToBitmapTex(diffuseTexmap);
        if (diffuseTexmap) {
            Class_ID cid = diffuseTexmap->ClassID();
            MLOG << " cid=(" << cid.PartA() << "," << cid.PartB() << ")"
                 << " hasBitmapTex=" << (diffuseBmpTex ? "yes" : "NO");
        }
        MLOG << std::endl;

        if (layer.textureIdTrackIndex >= 0 && diffuseBmpTex)
        {
            IFLGenResult ifl = generateIFLForLayer(layer, irModel, modelDir, resolver);
            if (!ifl.iflPath.empty()) {
                BitmapTex* bmpTex = diffuseBmpTex;

                // `texmaps` holds ONE Texmap per MDX texture index, so every
                // layer referencing the same texture shares this object.
                // Writing the IFL name straight onto it let the last layer
                // processed win for all of them: a model with five KMTF
                // tracks generated anim_t0..t4 but ended up with all six
                // flipbook materials pointing at anim_t4.ifl. Give this
                // layer its own copy and re-point the material at it, so
                // each track drives only its own material.
                DefaultRemapDir iflRemap;
                if (Texmap* cloneTm =
                        static_cast<Texmap*>(diffuseTexmap->Clone(iflRemap))) {
                    if (BitmapTex* cloneBmp = unwrapToBitmapTex(cloneTm)) {
                        bmpTex        = cloneBmp;
                        diffuseTexmap = cloneTm;

                        if (const wchar_t* dp =
                                textureSlotParamName(ir::TextureSlot::Diffuse))
                            pbSetTexmap(ref, dp, cloneTm);

                        // Mirror onto the StdMat2 delegate, same reason as
                        // the diffuse sync above.
                        for (int ri = 0; ri < mtl->NumRefs(); ri++) {
                            if (auto* sm =
                                    dynamic_cast<StdMat2*>(mtl->GetReference(ri))) {
                                sm->SetSubTexmap(ID_DI, cloneTm);
                                sm->EnableMap(ID_DI, TRUE);
                                break;
                            }
                        }

                        if (showInViewport) {
                            cloneTm->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED);
                            cloneBmp->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED);
                            mtl->SetActiveTexmap(cloneTm);
                        }
                        MLOG << "[IFL] cloned shared texmap for this layer"
                             << std::endl;
                    }
                }

                const std::string narrowPath = wlog(ifl.iflPath);
                bmpTex->SetMapName(ifl.iflPath.c_str());
                bmpTex->SetStartTime(ifl.startTime);
                bmpTex->SetPlaybackRate(ifl.playbackRate);
                bmpTex->SetEndCondition(ifl.endCondition);
                MLOG << "[IFL] SetMapName='" << narrowPath << "'"
                     << " startTime=" << ifl.startTime
                     << " playbackRate=" << ifl.playbackRate
                     << " endCond=" << ifl.endCondition << std::endl;
            } else {
                MLOG << "[IFL] generateIFLForLayer returned empty path — no IFL set"
                     << std::endl;
            }
        }
    }

    // Reforged PBR parameters
    pbSetFloat(ref, L"emissiveGain", layer.emissiveGain);
    pbSetFloat(ref, L"fresnelOpacity", layer.fresnelOpacity);
    pbSetFloat(ref, L"fresnelTeamCol", layer.fresnelTeamColor);
    pbSetFloat(ref, L"fresnelR", layer.fresnelColor.x);
    pbSetFloat(ref, L"fresnelG", layer.fresnelColor.y);
    pbSetFloat(ref, L"fresnelB", layer.fresnelColor.z);

    return mtl;
}

// ── Helper: CompositeMaterial param IDs (from SDK sample) ────

enum {
    compmat_mtls    = 0,
    compmat_type    = 1,
    compmat_map_on  = 2,
    compmat_amount  = 3
};

} // anonymous namespace

namespace mdx_scene {

std::vector<Mtl*> Wc3MaterialBuilder::buildMaterials(
    const ir::IRModel& irModel, bool importTextures,
    const std::wstring& modelDir, mdx_scene::TextureResolver* resolver,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    MLOG << "[CASC] buildMaterials: importTextures=" << importTextures
         << " resolver=" << (resolver ? "yes" : "null") << std::endl;

    // Step 1: Baseline — create 1 Wc3Bitmap per MDX texture entry (as before).
    // These are the "default" instances used by layers WITHOUT texture animation,
    // or layers whose animation happens to be the first one seen for that texture.
    std::vector<Texmap*> texmapsFlat;
    if (importTextures) {
        texmapsFlat.reserve(irModel.textures.size());
        for (const auto& irTex : irModel.textures)
            texmapsFlat.push_back(createNativeBitmap(irTex, modelDir, resolver, gi));
    }

    // Step 2: For layers that use a DIFFERENT texture animation on the same
    // texture, allocate additional bitmap instances so the animations don't
    // overwrite each other on a shared bitmap.
    //
    // Key format: textureIndex * 100000 + (taIdx + 1)
    //   → taIdx = -1 (no animation) maps to the baseline (slot 0).
    //   → the FIRST unique taIdx per texIdx also uses the baseline (no clone).
    //   → subsequent unique taIdx per texIdx get their own fresh bitmap.
    std::map<int64_t, Texmap*> bitmapCache;
    auto makeKey = [](int32_t texIdx, int32_t taIdx, int32_t uvSet) -> int64_t {
        return (static_cast<int64_t>(texIdx) * 100000LL +
                static_cast<int64_t>(taIdx + 1)) * 64LL +
               static_cast<int64_t>(uvSet + 1);
    };

    // The UV set belongs in the key alongside the animation. Both the texture
    // animation and the map channel are stored ON the texmap, so one shared
    // instance can only ever hold one of each: when two layers name the same
    // texture with different coordIds, whichever material is built last wins
    // and the other layer silently samples the wrong unwrap. That is not
    // hypothetical — LxX_GuFeng_STreeA01_00.mdx uses its shadow texture at
    // coordId 1 on the tree and coordId 0 on the ground quad.
    //
    // The first (animation, UV set) combination seen for a texture reuses the
    // baseline instance; every later distinct combination gets its own bitmap.
    std::set<int32_t> baselineClaimed;

    if (importTextures) {
        for (const auto& irMat : irModel.materials) {
            for (const auto& layer : irMat.layers) {
                int32_t taIdx = layer.textureAnimationIndex;
                int32_t uvSet = layer.uvSetIndex;
                for (const auto& texRef : layer.textureRefs) {
                    if (texRef.textureIndex < 0 ||
                        texRef.textureIndex >= (int32_t)irModel.textures.size())
                        continue;

                    int32_t texIdx = texRef.textureIndex;
                    int64_t key = makeKey(texIdx, taIdx, uvSet);

                    if (bitmapCache.find(key) != bitmapCache.end())
                        continue; // already allocated for this combination

                    if (baselineClaimed.insert(texIdx).second) {
                        bitmapCache[key] = texmapsFlat[texIdx];
                    } else {
                        const auto& irTex = irModel.textures[texIdx];
                        Texmap* bmp = createNativeBitmap(irTex, modelDir, resolver, gi);
                        bitmapCache[key] = bmp;
                        MLOG << "[TEX] Cloned bitmap for texIdx=" << texIdx
                             << " taIdx=" << taIdx << " uvSet=" << uvSet
                             << std::endl;
                    }
                }
            }
        }
    }

    // Lambda: returns a texmap vector (indexed by texture index) for a given
    // layer, picking the correct bitmap instance for that layer's animation.
    auto buildLayerTexmaps = [&](const ir::MaterialLayer& layer) -> std::vector<Texmap*> {
        // Start from the baseline — every texIdx has an entry, even if this
        // layer doesn't animate. Then override with animation-specific clones.
        std::vector<Texmap*> t = texmapsFlat;
        int32_t taIdx = layer.textureAnimationIndex;
        int32_t uvSet = layer.uvSetIndex;
        for (const auto& texRef : layer.textureRefs) {
            if (texRef.textureIndex < 0 ||
                texRef.textureIndex >= (int32_t)t.size())
                continue;
            int64_t key = makeKey(texRef.textureIndex, taIdx, uvSet);
            auto it = bitmapCache.find(key);
            if (it != bitmapCache.end())
                t[texRef.textureIndex] = it->second;
        }
        return t;
    };

    std::vector<Mtl*> materials;
    materials.reserve(irModel.materials.size());

    for (const auto& irMat : irModel.materials) {
        materials.push_back(buildWc3Material(
            irMat, irModel, texmapsFlat, buildLayerTexmaps,
            modelDir, resolver, gi, reporter));
    }

    return materials;
}

Mtl* Wc3MaterialBuilder::buildWc3Material(
    const ir::Material& irMat, const ir::IRModel& irModel,
    const std::vector<Texmap*>& texmapsFlat,
    const LayerTexmapsFn& buildLayerTexmaps,
    const std::wstring& modelDir,
    mdx_scene::TextureResolver* resolver,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    if (irMat.layers.empty())
        return nullptr;

    // Single layer → single Wc3Material (also used for Reforged PBR)
    if (irMat.layers.size() == 1) {
        auto layerTexmaps = buildLayerTexmaps(irMat.layers[0]);
        Mtl* mtl = buildSingleLayerWc3Material(
            irMat.layers[0], irMat, irModel, layerTexmaps, modelDir,
            resolver, gi);

        if (mtl) {
            if (!irMat.name.empty()) {
                MSTR name;
                name.printf(_T("%hs"), irMat.name.c_str());
                mtl->SetName(name);
            }
            return mtl;
        }

        // Fallback to StdMat2 if Wc3Material plugin not available
        return buildStdFallback(irMat, irModel, texmapsFlat, modelDir, gi);
    }

    // Multi-layer → CompositeMaterial wrapping Wc3Material children
    // (matches MaxScript getCompoundMaterial + per-layer Warcraft3())
    Mtl* compMtl = static_cast<Mtl*>(
        gi->CreateInstance(MATERIAL_CLASS_ID, COMPOSITE_MATERIAL_CLASS_ID));

    if (!compMtl) {
        // CompositeMaterial not available — fall back to first layer only
        auto layerTexmaps = buildLayerTexmaps(irMat.layers[0]);
        Mtl* mtl = buildSingleLayerWc3Material(
            irMat.layers[0], irMat, irModel, layerTexmaps, modelDir,
            resolver, gi);
        if (mtl && !irMat.name.empty()) {
            MSTR name;
            name.printf(_T("%hs"), irMat.name.c_str());
            mtl->SetName(name);
        }
        return mtl;
    }

    if (!irMat.name.empty()) {
        MSTR name;
        name.printf(_T("%hs"), irMat.name.c_str());
        compMtl->SetName(name);
    }

    // Access CompositeMaterial's ParamBlock2
    IParamBlock2* pb = nullptr;
    for (int i = 0; i < compMtl->NumRefs(); i++) {
        auto* ref = compMtl->GetReference(i);
        auto* candidate = dynamic_cast<IParamBlock2*>(ref);
        if (candidate) { pb = candidate; break; }
    }

    if (!pb) {
        // Can't access PBlock — fall back to first layer
        compMtl->DeleteMe();
        auto layerTexmaps = buildLayerTexmaps(irMat.layers[0]);
        Mtl* mtl = buildSingleLayerWc3Material(
            irMat.layers[0], irMat, irModel, layerTexmaps, modelDir,
            resolver, gi);
        return mtl;
    }

    int numLayers = static_cast<int>(irMat.layers.size());

    // Resize all Tab params to accommodate our layers.
    // CompositeMaterial's tabs are NOT parallel. compmat_mtls[0] is the BASE
    // material and the blend tabs describe only the layers composited ON TOP
    // of it, so blend index i governs compmat_mtls[i + 1] — which is why the
    // defaults are 10 materials against 9 of everything else. Indexing both
    // with the same j put each layer's blend settings on the layer above it
    // and left the base layer's settings governing layer 1.
    if (numLayers > pb->Count(compmat_mtls))
        pb->SetCount(compmat_mtls, numLayers);
    if (numLayers - 1 > pb->Count(compmat_type)) {
        pb->SetCount(compmat_type, numLayers - 1);
        pb->SetCount(compmat_map_on, numLayers - 1);
        pb->SetCount(compmat_amount, numLayers - 1);
    }

    for (int j = 0; j < numLayers; ++j) {
        // Each layer gets its own texmap vector — this ensures that when two
        // layers in the same composite reference the same texture with
        // different texture animations, each gets its own bitmap instance.
        auto layerTexmaps = buildLayerTexmaps(irMat.layers[j]);
        // Every sub gets its texture display-flagged (showInViewport=true).
        // The old "last layer only" policy made Nitrous render a 2-layer
        // form-switch composite as its alternate-form texture at full
        // opacity — per-layer opacity animation needs ALL subs displayable
        // so the viewport blends them.
        Mtl* subMtl = buildSingleLayerWc3Material(
            irMat.layers[j], irMat, irModel, layerTexmaps, modelDir,
            resolver, gi, true);

        if (subMtl) {
            MSTR subName;
            subName.printf(_T("Layer %d"), j + 1);
            subMtl->SetName(subName);
        }

        // Set sub-material
        pb->SetValue(compmat_mtls, 0, subMtl, j);

        // The base material has no blend entry of its own — it is what the
        // others are composited onto.
        if (j == 0) continue;

        const int blendIdx = j - 1;
        pb->SetValue(compmat_map_on, 0, TRUE, blendIdx);

        switch (irMat.layers[j].blendMode) {
        case ir::BlendMode::Additive:
        case ir::BlendMode::AddAlpha:
            pb->SetValue(compmat_type, 0, 1, blendIdx);      // 1 = Additive
            pb->SetValue(compmat_amount, 0, 50.0f, blendIdx);
            break;

        case ir::BlendMode::Modulate:
        case ir::BlendMode::Modulate2x:
            // WC3 MULTIPLIES these over what is already on screen, and
            // Composite Material offers only Mix / Additive / Subtractive.
            // There is no setting that comes close: compositing the layer at
            // all draws it as an opaque surface, which discards the base
            // layer's alpha cutout along with its colour (a tree's leaf cards
            // come out as solid grey quads, and the whole model turns the
            // colour of the modulate map). Leave the sub-material in place
            // but out of the composite so the base art reads correctly. The
            // layer still round-trips: extractCompositeMaterial() walks
            // GetSubMtl() and never consults mapEnables.
            pb->SetValue(compmat_type, 0, 0, blendIdx);
            pb->SetValue(compmat_amount, 0, 100.0f, blendIdx);
            pb->SetValue(compmat_map_on, 0, FALSE, blendIdx);
            break;

        default:
            pb->SetValue(compmat_type, 0, 0, blendIdx);      // 0 = Mix
            pb->SetValue(compmat_amount, 0, 100.0f, blendIdx);
            break;
        }
    }

    activateCompositeViewportLayer(compMtl, irMat, irModel, gi);

    return compMtl;
}

namespace {

bool isAdditiveBlend(ir::BlendMode mode) {
    return mode == ir::BlendMode::Additive || mode == ir::BlendMode::AddAlpha;
}

bool isModulateBlend(ir::BlendMode mode) {
    return mode == ir::BlendMode::Modulate || mode == ir::BlendMode::Modulate2x;
}

// A real bitmap on the diffuse slot, not a replaceable (team colour/glow) one.
bool hasOwnDiffuseTexture(const ir::MaterialLayer& layer, const ir::IRModel& irModel) {
    for (const auto& texRef : layer.textureRefs) {
        if (texRef.slot != ir::TextureSlot::Diffuse ||
            texRef.textureIndex < 0 ||
            texRef.textureIndex >= static_cast<int32_t>(irModel.textures.size()))
            continue;
        const auto& tex = irModel.textures[texRef.textureIndex];
        if (tex.replaceableId == 0 && !tex.filePath.empty())
            return true;
    }
    return false;
}

} // namespace

void activateCompositeViewportLayer(Mtl* compMtl, const ir::Material& irMat,
                                    const ir::IRModel& irModel, Interface* gi)
{
    if (!compMtl || !gi) return;
    const int numLayers = std::min(static_cast<int>(irMat.layers.size()),
                                   compMtl->NumSubMtls());
    if (numLayers < 2) return;

    // Nitrous has no shaded display for Composite: with a Wc3Material, a
    // Standard or any other sub-material as the base it draws the whole object
    // flat grey (tested on Max 2027). The only thing it shows is what is
    // switched on AT THE COMPOSITE, and SetActiveTexmap() alone does not do
    // that — Interface::ActivateTexture() on the top-level material does (the
    // SDK call behind MAXScript's showTextureMap).
    //
    // Switch on a whole LAYER MATERIAL, which is what the viewport menu's
    // "Shaded Materials with Maps" does: Nitrous then draws that layer with
    // its opacity map. A texmap carries no opacity, so a glow or trail texture
    // on black showed as a solid black plane (nwdg2.mdx, fm-zhaoyundg.mdx).
    //
    // Which layer reads as the model:
    //  1. the lowest opaque or alpha-tested layer with a real texture (a
    //     team-colour base has no bitmap, so a unit shows its skin above it);
    //  2. with additive layers, the last of them — the one the menu picks;
    //  3. the lowest blended layer with a real texture — additive and
    //     modulate overlays (glows, baked shadow maps, often on the second UV
    //     set) would hide the art;
    //  4. the topmost layer that is actually composited — a modulate layer is
    //     left out of the composite, so it must not be the one Nitrous shows.
    int shownLayer = -1;
    for (int j = 0; j < numLayers && shownLayer < 0; ++j) {
        const auto mode = irMat.layers[j].blendMode;
        if ((mode == ir::BlendMode::None || mode == ir::BlendMode::Transparent) &&
            hasOwnDiffuseTexture(irMat.layers[j], irModel))
            shownLayer = j;
    }
    for (int j = numLayers - 1; j >= 0 && shownLayer < 0; --j)
        if (isAdditiveBlend(irMat.layers[j].blendMode)) shownLayer = j;
    for (int j = 0; j < numLayers && shownLayer < 0; ++j) {
        if (isModulateBlend(irMat.layers[j].blendMode)) continue;
        if (hasOwnDiffuseTexture(irMat.layers[j], irModel))
            shownLayer = j;
    }
    if (shownLayer < 0)
        shownLayer = isModulateBlend(irMat.layers[numLayers - 1].blendMode)
                         ? numLayers - 2 : numLayers - 1;

    if (Mtl* shownSubMtl = compMtl->GetSubMtl(shownLayer))
        gi->ActivateTexture(shownSubMtl, compMtl);
}

// ── StdMat2 fallback (Wc3Material plugin not loaded) ────────

Mtl* Wc3MaterialBuilder::buildStdFallback(
    const ir::Material& irMat, const ir::IRModel& irModel,
    const std::vector<Texmap*>& texmaps,
    const std::wstring& modelDir, Interface* gi)
{
    auto* stdMtl = NewDefaultStdMat();
    if (!irMat.name.empty()) {
        MSTR name;
        name.printf(_T("%hs"), irMat.name.c_str());
        stdMtl->SetName(name);
    }

    if (!irMat.layers.empty()) {
        const auto& layer = irMat.layers[0];
        stdMtl->SetTwoSided(layer.twoSided ? TRUE : FALSE);
        stdMtl->SetOpacity(layer.alpha, 0);

        if (!layer.textureRefs.empty()) {
            int32_t texIdx = layer.textureRefs[0].textureIndex;
            if (texIdx >= 0 && texIdx < static_cast<int32_t>(texmaps.size()) && texmaps[texIdx]) {
                // Use the pre-created bitmap directly
                stdMtl->SetSubTexmap(ID_DI, texmaps[texIdx]);
                stdMtl->EnableMap(ID_DI, TRUE);
                texmaps[texIdx]->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED);
            } else if (texIdx >= 0 && texIdx < static_cast<int32_t>(irModel.textures.size())) {
                // No pre-created bitmap — create a plain BitmapTex
                const auto& irTex = irModel.textures[texIdx];
                if (!irTex.filePath.empty()) {
                    BitmapTex* bmpTex = NewDefaultBitmapTex();
                    auto wpath = resolveTexturePath(modelDir, toWstr(irTex.filePath));
                    bmpTex->SetMapName(wpath.c_str());
                    stdMtl->SetSubTexmap(ID_DI, bmpTex);
                    stdMtl->EnableMap(ID_DI, TRUE);
                    bmpTex->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED);
                }
            }
        }
    }

    return stdMtl;
}

} // namespace mdx_scene
