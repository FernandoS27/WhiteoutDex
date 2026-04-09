// MDLXImporter — Wc3MaterialBuilder implementation
#include "wc3_material_builder.h"
#include "texture_resolver.h"
#include "../mdlx_class_ids.h"

#include <scene/paramblock_reader.h>
#include <iparamb2.h>
#include <stdmat.h>
#include <bitmap.h>
#include <plugapi.h>

#if defined(WHITEOUT_HAS_CASC)
#include <whiteout/storages/casc/storage.h>
#include <whiteout/utils/blizzard_game_finder.h>
#endif

#include <algorithm>
#include <filesystem>
#include <fstream>

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

} // anonymous namespace

// ── Texture path resolution (in mdx_scene namespace for shared use) ──

namespace mdx_scene {

namespace {
/// Narrow a wide string (for logging only — non-ASCII becomes '?').
std::string wlog(const std::wstring& ws) {
    std::string s; s.reserve(ws.size());
    for (wchar_t c : ws) s += (c < 128) ? static_cast<char>(c) : '?';
    return s;
}
} // anonymous

/// Resolve an MDX-relative texture path to an actual file on disk.
/// MDX stores paths like "Textures\Hero_Diffuse.blp" — relative to game data root.
/// Users typically place textures next to the model file.
///
/// Search order (first match wins):
///   1. modelDir + relDir + stem + origExt   (e.g. <mypath>\Textures\gutz.dds)
///   2. modelDir + relDir + stem + altExt    (try .dds/.blp/.tga/.png/.tif)
///   3. modelDir + stem + origExt             (flat, no subdir)
///   4. modelDir + stem + altExt              (flat, alternatives)
///   5. Fallback: modelDir + relDir + stem + origExt (best guess, may not exist)
std::wstring resolveTexturePath(const std::wstring& modelDir, const std::wstring& relPath) {
    if (relPath.empty()) return {};

    // Normalize separators and trim trailing whitespace/nulls
    std::wstring normRel = relPath;
    std::replace(normRel.begin(), normRel.end(), L'/', L'\\');
    while (!normRel.empty() && (normRel.back() <= L' ' || normRel.back() == L'\0'))
        normRel.pop_back();
    // Also strip leading separators so "\Textures\x.blp" doesn't look absolute
    while (!normRel.empty() && (normRel.front() == L'\\' || normRel.front() == L'/'))
        normRel.erase(normRel.begin());
    if (normRel.empty()) return {};

    // Normalize modelDir separators and ensure trailing backslash
    std::wstring dir = modelDir;
    std::replace(dir.begin(), dir.end(), L'/', L'\\');
    if (!dir.empty() && dir.back() != L'\\')
        dir.push_back(L'\\');

    // Split normRel into relDir (including trailing separator) + fileName
    std::wstring relDir, fileName;
    {
        auto lastSep = normRel.find_last_of(L'\\');
        if (lastSep != std::wstring::npos) {
            relDir = normRel.substr(0, lastSep + 1);  // includes separator
            fileName = normRel.substr(lastSep + 1);
        } else {
            fileName = normRel;
        }
    }

    // Split fileName into stem + extension
    std::wstring stem, origExt;
    {
        auto dotPos = fileName.find_last_of(L'.');
        if (dotPos != std::wstring::npos) {
            stem = fileName.substr(0, dotPos);
            origExt = fileName.substr(dotPos);
        } else {
            stem = fileName;
        }
    }
    std::wstring origExtLower = origExt;
    std::transform(origExtLower.begin(), origExtLower.end(), origExtLower.begin(), ::towlower);

    MLOG << "[TEX] resolveTexturePath dir='" << wlog(dir)
         << "' rel='" << wlog(normRel) << "'" << std::endl;

    // Extensions to try (in order of preference)
    static const std::wstring altExts[] = { L".dds", L".blp", L".tga", L".png", L".tif" };

    auto tryPath = [&](const std::wstring& candidate) -> bool {
        std::error_code ec;
        bool ok = std::filesystem::exists(candidate, ec);
        MLOG << "[TEX]   try '" << wlog(candidate) << "' -> "
             << (ok ? "FOUND" : "not found") << std::endl;
        return ok;
    };

    // 1) Original path with original extension
    if (!origExt.empty()) {
        std::wstring candidate = dir + relDir + stem + origExt;
        if (tryPath(candidate)) return candidate;
    }

    // 2) Original subdirectory with alternative extensions
    for (const auto& ext : altExts) {
        if (ext == origExtLower) continue;
        std::wstring candidate = dir + relDir + stem + ext;
        if (tryPath(candidate)) return candidate;
    }

    // 3) Flat (filename only, no subdir) with original extension
    if (!relDir.empty() && !origExt.empty()) {
        std::wstring candidate = dir + stem + origExt;
        if (tryPath(candidate)) return candidate;
    }

    // 4) Flat with alternative extensions
    if (!relDir.empty()) {
        for (const auto& ext : altExts) {
            if (ext == origExtLower) continue;
            std::wstring candidate = dir + stem + ext;
            if (tryPath(candidate)) return candidate;
        }
    }

    // Fallback: original path with original extension (may not exist on disk)
    std::wstring fallback = dir + relDir + stem + (origExt.empty() ? L".dds" : origExt);
    MLOG << "[TEX]   fallback '" << wlog(fallback) << "'" << std::endl;
    return fallback;
}

// ── CASC texture extraction ────────────────────────────────

#if defined(WHITEOUT_HAS_CASC)

namespace casc = whiteout::storages::casc;

/// Open the Warcraft III Reforged CASC archive.
/// Uses cascDir if non-empty, otherwise auto-detects via blizzard_game_finder.
std::optional<casc::Storage> openWc3CascStorage(const std::wstring& cascDir) {
    std::string path;

    if (!cascDir.empty()) {
        // Convert wide to narrow (ASCII-safe for install paths)
        path.reserve(cascDir.size());
        for (wchar_t c : cascDir) path += static_cast<char>(c);
        MLOG << "[CASC] Using provided cascDir: " << path << std::endl;
    } else {
        MLOG << "[CASC] Auto-detecting WC3 Reforged install..." << std::endl;
        auto games = whiteout::utils::findBlizzardGames();
        MLOG << "[CASC] Found " << games.size() << " Blizzard game(s)" << std::endl;
        for (const auto& g : games) {
            MLOG << "[CASC]   " << g.name << " -> " << g.path << std::endl;
            if (g.game == whiteout::utils::BlizzardGame::WarcraftIIIReforged ||
                g.game == whiteout::utils::BlizzardGame::WarcraftIII) {
                path = g.path;
                break;
            }
        }
    }

    if (path.empty()) {
        MLOG << "[CASC] No WC3 Reforged path found" << std::endl;
        return std::nullopt;
    }

    MLOG << "[CASC] Opening storage at: " << path << std::endl;
    auto storage = casc::Storage::open(path);
    if (!storage)
        MLOG << "[CASC] Failed to open storage" << std::endl;
    else
        MLOG << "[CASC] Storage opened successfully" << std::endl;
    return storage;
}

/// Try to find and extract a texture from the CASC archive.
/// relPath: MDX-relative path like "Textures\RibbonBlur.blp"
/// Searches war3.w3mod: and war3.w3mod:_hd.w3mod: prefixes,
/// case-insensitive, trying all common extensions.
/// On success, extracts the file to modelDir preserving the relative path
/// and returns the absolute path to the extracted file.
std::wstring extractTextureFromCASC(
    casc::Storage& storage,
    const std::wstring& modelDir,
    const std::wstring& relPath)
{
    namespace fs = std::filesystem;
    if (relPath.empty()) return {};

    // Normalize to lowercase with backslashes (CASC convention)
    std::string cascRel;
    cascRel.reserve(relPath.size());
    for (wchar_t c : relPath) {
        char ch = static_cast<char>(c);
        if (ch == '/') ch = '\\';
        cascRel += static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    }

    // Strip extension — we'll try all common ones
    std::string cascStem = cascRel;
    auto dotPos = cascStem.rfind('.');
    if (dotPos != std::string::npos)
        cascStem.resize(dotPos);

    // Prefixes to search (SD first, then HD)
    static const char* kPrefixes[] = {
        "war3.w3mod:",
        "war3.w3mod:_hd.w3mod:",
    };

    // Extensions to try
    static const char* kExts[] = { ".dds", ".blp", ".tga", ".png", ".tif" };

    MLOG << "[CASC] Searching for: " << cascStem << " (original rel: " << cascRel << ")" << std::endl;

    for (const char* prefix : kPrefixes) {
        for (const char* ext : kExts) {
            std::string cascPath = std::string(prefix) + cascStem + ext;

            auto data = storage.readFile(cascPath);
            if (!data || data->empty())
                continue;

            MLOG << "[CASC] Found: " << cascPath << " (" << data->size() << " bytes)" << std::endl;

            // Build local output path: modelDir + relative path with found extension
            fs::path outRel = fs::path(cascStem).make_preferred();
            std::wstring outRelW = outRel.wstring() + toWstr(ext);
            fs::path outPath = fs::path(modelDir) / outRelW;

            // Create parent directories if needed
            std::error_code ec;
            fs::create_directories(outPath.parent_path(), ec);

            // Write the file
            std::ofstream ofs(outPath, std::ios::binary);
            if (!ofs) {
                MLOG << "[CASC] Failed to write: " << outPath.string() << std::endl;
                continue;
            }
            ofs.write(reinterpret_cast<const char*>(data->data()), data->size());
            ofs.close();

            if (ofs.good())
                return outPath.wstring();
        }
    }

    return {};
}

#endif // WHITEOUT_HAS_CASC

// ── Wc3Bitmap texture map creation ──────────────────────────

/// Resolve texture path. When CASC is available and cascStorage is non-null,
/// textures not found locally are extracted from the Reforged archive.
std::wstring resolveTexturePathFull(
    const std::wstring& modelDir, const std::wstring& relPath,
    void* cascStorage)
{
    namespace fs = std::filesystem;
    auto wpath = resolveTexturePath(modelDir, relPath);

    // If the resolved path exists on disk, use it
    if (!wpath.empty()) {
        std::error_code ec;
        if (fs::exists(wpath, ec))
            return wpath;
    }

#if defined(WHITEOUT_HAS_CASC)
    // Try CASC extraction
    if (cascStorage) {
        auto* storage = static_cast<whiteout::storages::casc::Storage*>(cascStorage);
        auto extracted = extractTextureFromCASC(*storage, modelDir, relPath);
        if (!extracted.empty())
            return extracted;
    }
#endif

    // Return the best-guess path (even if file doesn't exist)
    return wpath;
}

void* openCascStorage(const std::wstring& cascDir) {
#if defined(WHITEOUT_HAS_CASC)
    auto storage = openWc3CascStorage(cascDir);
    if (storage)
        return new whiteout::storages::casc::Storage(std::move(*storage));
#endif
    return nullptr;
}

void closeCascStorage(void* storage) {
#if defined(WHITEOUT_HAS_CASC)
    delete static_cast<whiteout::storages::casc::Storage*>(storage);
#endif
}

} // namespace mdx_scene

namespace {

using mdx_scene::resolveTexturePathFull;

Texmap* createWc3Bitmap(const ir::Texture& irTex, const std::wstring& modelDir,
                        void* cascStorage, Interface* gi)
{
    MLOG << "[TEX] createWc3Bitmap for '" << irTex.filePath << "'" << std::endl;

    Texmap* tex = static_cast<Texmap*>(
        gi->CreateInstance(TEXMAP_CLASS_ID, mdx_ids::WC3_BITMAP));

    if (!tex) {
        MLOG << "[TEX]   Wc3Bitmap plugin unavailable, using default BitmapTex" << std::endl;
        // Fallback: standard BitmapTex
        BitmapTex* bmpTex = NewDefaultBitmapTex();
        if (!irTex.filePath.empty()) {
            auto wpath = resolveTexturePathFull(modelDir, toWstr(irTex.filePath), cascStorage);
            bmpTex->SetMapName(wpath.c_str());
        }
        return bmpTex;
    }

    // Set file path on the BitmapTex delegate (found in references)
    if (!irTex.filePath.empty()) {
        auto wpath = resolveTexturePathFull(modelDir, toWstr(irTex.filePath), cascStorage);
        bool delegateFound = false;
        for (int i = 0; i < tex->NumRefs(); i++) {
            ReferenceTarget* ref = tex->GetReference(i);
            if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                static_cast<BitmapTex*>(ref)->SetMapName(wpath.c_str());
                delegateFound = true;
                MLOG << "[TEX]   SetMapName on delegate (refIdx=" << i << ")" << std::endl;
                break;
            }
        }
        if (!delegateFound)
            MLOG << "[TEX]   WARNING: no BitmapTex delegate found among "
                 << tex->NumRefs() << " refs" << std::endl;
    }

    // Set custom properties
    auto* ref = dynamic_cast<ReferenceTarget*>(tex);
    if (ref) {
        // replaceableId is 1-based in the plugin: 1 = Not Used, 2+ = replaceable types
        pbSetInt(ref, L"replaceableId", irTex.replaceableId + 1);
        pbSetBool(ref, L"wrapU", irTex.wrapU ? TRUE : FALSE);
        pbSetBool(ref, L"wrapV", irTex.wrapV ? TRUE : FALSE);
    }

    return tex;
}

// Map ir::BlendMode to filterMode int (1-based)
int blendModeToFilterMode(ir::BlendMode bm) {
    switch (bm) {
    case ir::BlendMode::Opaque:     return 1;
    case ir::BlendMode::AlphaKey:   return 2;
    case ir::BlendMode::Alpha:      return 3;
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

} // anonymous namespace

namespace mdx_scene {

// ── Helper: Build a single Wc3Material from one layer ────────

static Mtl* buildSingleLayerWc3Material(
    const ir::MaterialLayer& layer,
    const ir::Material& irMat,
    const std::vector<Texmap*>& texmaps,
    Interface* gi,
    bool showInViewport = true)
{
    Mtl* mtl = static_cast<Mtl*>(
        gi->CreateInstance(MATERIAL_CLASS_ID, mdx_ids::WC3_MATERIAL));
    if (!mtl) return nullptr;

    auto* ref = dynamic_cast<ReferenceTarget*>(mtl);
    if (!ref) return mtl;

    // Shader type: 1 = SD (Classic), 2 = HD (Reforged PBR)
    // HD layers have PBR texture slots (Normal/ORM/Emissive/Environment) or a shader path.
    bool isHD = !irMat.shaderName.empty();
    if (!isHD) {
        for (const auto& texRef : layer.textureRefs) {
            if (texRef.slot != ir::TextureSlot::Diffuse &&
                texRef.slot != ir::TextureSlot::TeamColor) {
                isHD = true;
                break;
            }
        }
    }
    pbSetInt(ref, L"shaderType", isHD ? 2 : 1);

    // Filter mode (1-based)
    pbSetInt(ref, L"filterMode", blendModeToFilterMode(layer.blendMode));

    // Opacity (0-100)
    pbSetFloat(ref, L"opacity", layer.alpha * 100.0f);

    // Shading flags
    pbSetBool(ref, L"twoSided", layer.twoSided ? TRUE : FALSE);
    pbSetBool(ref, L"unshaded", layer.unshaded ? TRUE : FALSE);
    pbSetBool(ref, L"unfogged", layer.unfogged ? TRUE : FALSE);
    pbSetBool(ref, L"noDepthTest", layer.noDepthTest ? TRUE : FALSE);
    pbSetBool(ref, L"noDepthSet", layer.noDepthWrite ? TRUE : FALSE);

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

            if (texRef.slot == ir::TextureSlot::Diffuse) {
                diffuseTexmap = texmap;
                // sphereEnvMap stored on diffuse Wc3Bitmap for round-trip
                // (exporter reads it from diffuseMap; callback sets
                //  delegate.coords.mappingType for viewport sphere mapping)
                if (layer.sphereEnvMap) {
                    auto* bmpRef = dynamic_cast<ReferenceTarget*>(texmap);
                    if (bmpRef) pbSetBool(bmpRef, L"sphereEnvMap", TRUE);
                }
            }
        }

        if (diffuseTexmap && showInViewport) {
            diffuseTexmap->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED);
            mtl->SetActiveTexmap(diffuseTexmap);
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
    const std::wstring& modelDir, void* cascStorage,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    MLOG << "[CASC] buildMaterials: importTextures=" << importTextures
         << " cascStorage=" << (cascStorage ? "yes" : "null") << std::endl;

    // Pre-create 1 Wc3Bitmap per MDX texture entry.
    // These are shared (instanced) across all materials/layers that reference
    // the same texture index, matching the MDX 1:1 texture-entry-to-bitmap model.
    std::vector<Texmap*> texmaps;
    if (importTextures) {
        texmaps.reserve(irModel.textures.size());
        for (const auto& irTex : irModel.textures)
            texmaps.push_back(createWc3Bitmap(irTex, modelDir, cascStorage, gi));
    }

    std::vector<Mtl*> materials;
    materials.reserve(irModel.materials.size());

    for (const auto& irMat : irModel.materials) {
        materials.push_back(buildWc3Material(irMat, irModel, texmaps, modelDir, gi, reporter));
    }

    return materials;
}

Mtl* Wc3MaterialBuilder::buildWc3Material(
    const ir::Material& irMat, const ir::IRModel& irModel,
    const std::vector<Texmap*>& texmaps,
    const std::wstring& modelDir, Interface* gi, core::ExportErrorReporter& reporter)
{
    if (irMat.layers.empty())
        return nullptr;

    // Single layer → single Wc3Material (also used for Reforged PBR)
    if (irMat.layers.size() == 1) {
        Mtl* mtl = buildSingleLayerWc3Material(
            irMat.layers[0], irMat, texmaps, gi);

        if (mtl) {
            if (!irMat.name.empty()) {
                MSTR name;
                name.printf(_T("%hs"), irMat.name.c_str());
                mtl->SetName(name);
            }
            return mtl;
        }

        // Fallback to StdMat2 if Wc3Material plugin not available
        return buildStdFallback(irMat, irModel, texmaps, modelDir, gi);
    }

    // Multi-layer → CompositeMaterial wrapping Wc3Material children
    // (matches MaxScript getCompoundMaterial + per-layer Warcraft3())
    Mtl* compMtl = static_cast<Mtl*>(
        gi->CreateInstance(MATERIAL_CLASS_ID, COMPOSITE_MATERIAL_CLASS_ID));

    if (!compMtl) {
        // CompositeMaterial not available — fall back to first layer only
        Mtl* mtl = buildSingleLayerWc3Material(
            irMat.layers[0], irMat, texmaps, gi);
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
        Mtl* mtl = buildSingleLayerWc3Material(
            irMat.layers[0], irMat, texmaps, gi);
        return mtl;
    }

    int numLayers = static_cast<int>(irMat.layers.size());

    // Resize all Tab params to accommodate our layers.
    // CompositeMaterial starts with 10 slots by default; grow if needed.
    if (numLayers > pb->Count(compmat_mtls)) {
        pb->SetCount(compmat_mtls, numLayers);
        pb->SetCount(compmat_type, numLayers);
        pb->SetCount(compmat_map_on, numLayers);
        pb->SetCount(compmat_amount, numLayers);
    }

    for (int j = 0; j < numLayers; ++j) {
        bool isLastLayer = (j == numLayers - 1);
        Mtl* subMtl = buildSingleLayerWc3Material(
            irMat.layers[j], irMat, texmaps, gi, isLastLayer);

        if (subMtl) {
            MSTR subName;
            subName.printf(_T("Layer %d"), j + 1);
            subMtl->SetName(subName);
        }

        // Set sub-material
        pb->SetValue(compmat_mtls, 0, subMtl, j);

        // Enable layer
        pb->SetValue(compmat_map_on, 0, TRUE, j);

        // Blend type: additive filters (4=Additive, 5=AddAlpha) use additive mix
        int filterMode = blendModeToFilterMode(irMat.layers[j].blendMode);
        if (filterMode >= 4 && filterMode <= 5) {
            pb->SetValue(compmat_type, 0, 1, j);     // 1 = Additive
            pb->SetValue(compmat_amount, 0, 50.0f, j);
        } else {
            pb->SetValue(compmat_type, 0, 0, j);     // 0 = Normal
            pb->SetValue(compmat_amount, 0, 100.0f, j);
        }
    }

    // Show the last layer's texture in the viewport
    Mtl* lastSubMtl = compMtl->GetSubMtl(numLayers - 1);
    if (lastSubMtl) {
        MtlBase* activeTex = lastSubMtl->GetActiveTexmap();
        if (activeTex)
            compMtl->SetActiveTexmap(static_cast<Texmap*>(activeTex));
    }

    return compMtl;
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
        stdMtl->SetOpacity(layer.alpha * 100.0f, 0);

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
