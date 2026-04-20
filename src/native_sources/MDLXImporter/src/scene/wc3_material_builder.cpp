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
#include <map>
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

    // Extensions to try (textures + model files for PE1 particle models)
    static const char* kExts[] = { ".dds", ".blp", ".tga", ".png", ".tif", ".mdx", ".mdl" };

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

/// Create a native 3ds Max BitmapTex for one IR texture entry.
/// Sets file path and tiling (wrapU/wrapV) on the StdUVGen.
/// replaceableId is NOT set here — it is stored on the Wc3Material later
/// (see buildSingleLayerWc3Material).
/// sphereEnvMap is also set on the Wc3Material level via StdUVGen.
Texmap* createNativeBitmap(const ir::Texture& irTex, const std::wstring& modelDir,
                           void* cascStorage, Interface* /*gi*/)
{
    MLOG << "[TEX] createNativeBitmap for '" << irTex.filePath << "'" << std::endl;

    BitmapTex* bmpTex = NewDefaultBitmapTex();
    if (!bmpTex) {
        MLOG << "[TEX]   ERROR: NewDefaultBitmapTex() returned null" << std::endl;
        return nullptr;
    }

    // Set the file path
    if (!irTex.filePath.empty()) {
        auto wpath = resolveTexturePathFull(modelDir, toWstr(irTex.filePath), cascStorage);
        bmpTex->SetMapName(wpath.c_str());
        MLOG << "[TEX]   SetMapName '" << irTex.filePath << "'" << std::endl;
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

    return bmpTex;
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
    void* cascStorage = nullptr)
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
        std::wstring fullPath = resolveTexturePathFull(
            modelDir, toWstr(irTex.filePath), cascStorage);

        // Convert wide path to UTF-8 for IFL file (narrow ASCII output).
        // Standard library's wstring→string conversion via std::string
        // constructor assumes the source is ASCII-compatible which is
        // fine for typical Windows filesystem paths.
        std::string narrow(fullPath.begin(), fullPath.end());
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

    MLOG << "[IFL-DEBUG] writing IFL file to: " << iflPath.string() << std::endl;

    std::ofstream ofs(iflPath, std::ios::binary);
    if (!ofs) {
        MLOG << "[IFL] Failed to open for write: " << iflPath.string() << std::endl;
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

// ── Helper: Build a single Wc3Material from one layer ────────

static Mtl* buildSingleLayerWc3Material(
    const ir::MaterialLayer& layer,
    const ir::Material& irMat,
    const ir::IRModel& irModel,
    const std::vector<Texmap*>& texmaps,
    const std::wstring& modelDir,
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
    // MDX-Semantik: Jede Texture im TEXS-chunk kann einen replaceableId haben
    // (0 = normal, 1 = TeamColor, 2 = TeamGlow, 3+ = Cliff/Tree/etc.).
    // Im Wc3Material Plugin gibt es zwei VERSCHIEDENE Konstrukte für TC:
    //
    //   Konstrukt A — reines TC-Material (SD/classic):
    //     ddReplaceable = "Team Color"      (Dropdown)
    //     diffuseMap = NONE
    //     → Ganzes Material wird zur TC-Fläche
    //
    //   Konstrukt B — HD-Material mit TC-Mask (Reforged):
    //     ddReplaceable = "Not Used"        (Dropdown bleibt leer)
    //     diffuseMap   = main_diffuse.dds
    //     teamColorMap = TC_mask.dds        (dedizierter Slot)
    //     → Mask bestimmt wo TC-Tint angewendet wird
    //
    // Diskriminator: der SLOT der Textur-Referenz, nicht nur der replaceableId.
    //   slot == Diffuse  mit replaceableId > 0 → Konstrukt A, Dropdown setzen
    //   slot == TeamColor (HD-Sub)             → Konstrukt B, teamColorMap-Slot
    //                                            wird unten im Texmap-Loop
    //                                            gesetzt; Dropdown bleibt 0
    //
    // Historischer Bug: Ein früherer Versuch nahm max(replaceableId) über alle
    // Texture-Refs. Das funktionierte für SD aber ruinierte HD-Materials:
    // Arthas v1200 hat Diffuse(replId=0) + TC-Sub(replId=1) → max=1 → Dropdown
    // fälschlich "Team Color" → Exporter löschte dann den Diffuse-Pfad.
    // NeoDex's Äquivalent: NeoDexSceneRebuilder.ms lines 1068-1073
    //   (teamColorTexId für HD separat, layer.retexture nur für SD-Diffuse).
    //
    // Dropdown-Mapping (Wc3Material.ms line 411):
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
                if (layer.sphereEnvMap) {
                    if (texmap->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                        BitmapTex* bmpTex = static_cast<BitmapTex*>(texmap);
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
            mtl->SetActiveTexmap(diffuseTexmap);
        }

        // ── IFL texture animation (KMTF track in MDX) ──
        // Following NeoDex's proven approach: generate an .ifl file with
        // a simple filename-per-line list, then use BitmapTex's native
        // playback controls to drive timing.
        MLOG << "[IFL-CHECK] layer.textureIdTrackIndex=" << layer.textureIdTrackIndex
             << " diffuseTexmap=" << (diffuseTexmap ? "yes" : "null");
        if (diffuseTexmap) {
            Class_ID cid = diffuseTexmap->ClassID();
            MLOG << " cid=(" << cid.PartA() << "," << cid.PartB() << ")"
                 << " isBMTEX=" << (cid == Class_ID(BMTEX_CLASS_ID, 0) ? "yes" : "NO");
        }
        MLOG << std::endl;

        if (layer.textureIdTrackIndex >= 0 && diffuseTexmap &&
            diffuseTexmap->ClassID() == Class_ID(BMTEX_CLASS_ID, 0))
        {
            IFLGenResult ifl = generateIFLForLayer(layer, irModel, modelDir);
            if (!ifl.iflPath.empty()) {
                BitmapTex* bmpTex = static_cast<BitmapTex*>(diffuseTexmap);
                std::string narrowPath(ifl.iflPath.begin(), ifl.iflPath.end());
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
    const std::wstring& modelDir, void* cascStorage,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    MLOG << "[CASC] buildMaterials: importTextures=" << importTextures
         << " cascStorage=" << (cascStorage ? "yes" : "null") << std::endl;

    // Step 1: Baseline — create 1 Wc3Bitmap per MDX texture entry (as before).
    // These are the "default" instances used by layers WITHOUT texture animation,
    // or layers whose animation happens to be the first one seen for that texture.
    std::vector<Texmap*> texmapsFlat;
    if (importTextures) {
        texmapsFlat.reserve(irModel.textures.size());
        for (const auto& irTex : irModel.textures)
            texmapsFlat.push_back(createNativeBitmap(irTex, modelDir, cascStorage, gi));
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
    auto makeKey = [](int32_t texIdx, int32_t taIdx) -> int64_t {
        return static_cast<int64_t>(texIdx) * 100000LL +
               static_cast<int64_t>(taIdx + 1);
    };

    // Track the first VALID animation (taIdx >= 0) assigned to each texture.
    // The first one reuses the baseline; later DIFFERENT valid animations get
    // fresh bitmap instances. taIdx == -1 means "no animation" and always
    // shares the baseline (never triggers a clone).
    std::map<int32_t, int32_t> firstTaForTex;  // texIdx → first valid taIdx seen

    if (importTextures) {
        for (const auto& irMat : irModel.materials) {
            for (const auto& layer : irMat.layers) {
                int32_t taIdx = layer.textureAnimationIndex;
                for (const auto& texRef : layer.textureRefs) {
                    if (texRef.textureIndex < 0 ||
                        texRef.textureIndex >= (int32_t)irModel.textures.size())
                        continue;

                    int32_t texIdx = texRef.textureIndex;
                    int64_t key = makeKey(texIdx, taIdx);

                    if (bitmapCache.find(key) != bitmapCache.end())
                        continue; // already allocated for this (texIdx, taIdx)

                    // No animation → always share the baseline bitmap
                    if (taIdx < 0) {
                        bitmapCache[key] = texmapsFlat[texIdx];
                        continue;
                    }

                    auto fit = firstTaForTex.find(texIdx);
                    if (fit == firstTaForTex.end()) {
                        // First valid animation for this texture → reuse baseline
                        firstTaForTex[texIdx] = taIdx;
                        bitmapCache[key] = texmapsFlat[texIdx];
                    } else if (fit->second == taIdx) {
                        // Same animation as first → reuse baseline (already in cache)
                        bitmapCache[key] = texmapsFlat[texIdx];
                    } else {
                        // Different valid animation on same texture → fresh bitmap
                        const auto& irTex = irModel.textures[texIdx];
                        Texmap* bmp = createNativeBitmap(irTex, modelDir, cascStorage, gi);
                        bitmapCache[key] = bmp;
                        MLOG << "[TEX-ANIM] Cloned bitmap for texIdx=" << texIdx
                             << " taIdx=" << taIdx
                             << " (baseline taIdx=" << fit->second << ")" << std::endl;
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
        for (const auto& texRef : layer.textureRefs) {
            if (texRef.textureIndex < 0 ||
                texRef.textureIndex >= (int32_t)t.size())
                continue;
            int64_t key = makeKey(texRef.textureIndex, taIdx);
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
            modelDir, gi, reporter));
    }

    return materials;
}

Mtl* Wc3MaterialBuilder::buildWc3Material(
    const ir::Material& irMat, const ir::IRModel& irModel,
    const std::vector<Texmap*>& texmapsFlat,
    const LayerTexmapsFn& buildLayerTexmaps,
    const std::wstring& modelDir, Interface* gi, core::ExportErrorReporter& reporter)
{
    if (irMat.layers.empty())
        return nullptr;

    // Single layer → single Wc3Material (also used for Reforged PBR)
    if (irMat.layers.size() == 1) {
        auto layerTexmaps = buildLayerTexmaps(irMat.layers[0]);
        Mtl* mtl = buildSingleLayerWc3Material(
            irMat.layers[0], irMat, irModel, layerTexmaps, modelDir, gi);

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
            irMat.layers[0], irMat, irModel, layerTexmaps, modelDir, gi);
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
            irMat.layers[0], irMat, irModel, layerTexmaps, modelDir, gi);
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
        // Each layer gets its own texmap vector — this ensures that when two
        // layers in the same composite reference the same texture with
        // different texture animations, each gets its own bitmap instance.
        auto layerTexmaps = buildLayerTexmaps(irMat.layers[j]);
        Mtl* subMtl = buildSingleLayerWc3Material(
            irMat.layers[j], irMat, irModel, layerTexmaps, modelDir, gi, isLastLayer);

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
