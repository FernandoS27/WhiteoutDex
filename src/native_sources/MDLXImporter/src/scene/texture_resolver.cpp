// MDLXImporter — Unified texture/model path resolver.
#include "texture_resolver.h"

#if defined(WHITEOUT_HAS_CASC)
#include <whiteout/storages/casc/storage.h>
#include <whiteout/utils/blizzard_game_finder.h>
#endif

#ifndef WHITEOUT_HAS_MPQ
#define WHITEOUT_HAS_MPQ 1
#endif

#include <whiteout/storages/mpq/storage.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

namespace mdx_scene {

namespace fs = std::filesystem;

// ============================================================================
// Logging
// ============================================================================

namespace {

// Importer-wide debug log. Same target as wc3_material_builder.cpp's MLOG
// stream, but opened independently here so the resolver can log without a
// translation-unit dependency.
std::ofstream& resLog() {
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
#define RLOG resLog()

// Narrow a wide string for ostream output. Non-ASCII becomes '?'; we use
// this for log paths only.
std::string wlog(const std::wstring& ws) {
    std::string s;
    s.reserve(ws.size());
    for (wchar_t c : ws)
        s += (c < 128) ? static_cast<char>(c) : '?';
    return s;
}

// Narrow an ASCII-only wide string to std::string. MDX paths are ASCII.
std::string mdxWideToNarrow(const std::wstring& wp) {
    std::string out;
    out.reserve(wp.size());
    for (wchar_t c : wp)
        out += static_cast<char>(c);
    return out;
}

std::wstring narrowToWide(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    for (unsigned char c : s)
        out += static_cast<wchar_t>(c);
    return out;
}

// Replace the extension on `rel` with `newExt` (includes the dot). Honours
// subdirectories so `Textures\Foo.tif` -> `Textures\Foo.dds`, not
// `Textures\Foo.tif.dds`.
std::wstring swapExtension(const std::wstring& rel, const std::wstring& newExt) {
    auto dot = rel.find_last_of(L'.');
    auto sep = rel.find_last_of(L"\\/");
    if (dot != std::wstring::npos && (sep == std::wstring::npos || dot > sep))
        return rel.substr(0, dot) + newExt;
    return rel + newExt;
}

std::string swapExtensionA(const std::string& rel, const std::string& newExt) {
    auto dot = rel.find_last_of('.');
    auto sep = rel.find_last_of("\\/");
    if (dot != std::string::npos && (sep == std::string::npos || dot > sep))
        return rel.substr(0, dot) + newExt;
    return rel + newExt;
}

// Extension aliases (lowercase, with leading dot), grouped by asset type.
// The MDX-stated extension is always tried first; the set picked by
// AltsForExt is the canonical fallback order applied at every tier.
// Sets are intentionally disjoint so a texture lookup never wastes a
// probe on a model/vfx extension and vice versa.
constexpr const wchar_t* kTexExtsW[] = {
    L".dds", L".blp", L".tga", L".png", L".tif", L".tiff",
    L".jpg", L".jpeg", L".bmp", L".psd", L".gif", L".hdr",
};
constexpr const char* kTexExtsA[] = {
    ".dds", ".blp", ".tga", ".png", ".tif", ".tiff",
    ".jpg", ".jpeg", ".bmp", ".psd", ".gif", ".hdr",
};
constexpr const wchar_t* kModelExtsW[] = { L".mdx", L".mdl" };
constexpr const char* kModelExtsA[] = { ".mdx", ".mdl" };
// Popcorn FX runtime payload (binary baked) vs source (XML). Reforged ships
// .pkb under the runtime path; some pipelines emit .pkfx instead so we
// treat them as aliases of each other.
constexpr const wchar_t* kVfxExtsW[] = { L".pkb", L".pkfx" };
constexpr const char* kVfxExtsA[] = { ".pkb", ".pkfx" };

template <typename C>
bool extInList(const std::basic_string<C>& ext, const C* const* list, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        if (ext == list[i]) return true;
    return false;
}

// Pick the wide alias set for an MDX-stated extension. Returns
// (list, count); when the extension is unknown, returns (nullptr, 0) so
// the caller only tries the MDX-stated form (no alias probing).
std::pair<const wchar_t* const*, std::size_t> AltsForExtW(const std::wstring& origExtLower) {
    if (origExtLower.empty())
        return { nullptr, 0 };
    if (extInList(origExtLower, kTexExtsW, std::size(kTexExtsW)))
        return { kTexExtsW, std::size(kTexExtsW) };
    if (extInList(origExtLower, kModelExtsW, std::size(kModelExtsW)))
        return { kModelExtsW, std::size(kModelExtsW) };
    if (extInList(origExtLower, kVfxExtsW, std::size(kVfxExtsW)))
        return { kVfxExtsW, std::size(kVfxExtsW) };
    return { nullptr, 0 };
}

std::pair<const char* const*, std::size_t> AltsForExtA(const std::string& origExtLower) {
    if (origExtLower.empty())
        return { nullptr, 0 };
    if (extInList(origExtLower, kTexExtsA, std::size(kTexExtsA)))
        return { kTexExtsA, std::size(kTexExtsA) };
    if (extInList(origExtLower, kModelExtsA, std::size(kModelExtsA)))
        return { kModelExtsA, std::size(kModelExtsA) };
    if (extInList(origExtLower, kVfxExtsA, std::size(kVfxExtsA)))
        return { kVfxExtsA, std::size(kVfxExtsA) };
    return { nullptr, 0 };
}

// Try every alias at one subdir tier. MDX-stated extension comes first,
// then aliases from the appropriate per-purpose set (skipping the MDX one).
// Returns the matched path or empty.
std::wstring tryDiskAtTier(const std::wstring& dirWithSlash,
                           const std::wstring& tierWithSlash,
                           const std::wstring& stem,
                           const std::wstring& origExt,
                           const std::wstring& origExtLower) {
    auto tryPath = [](const std::wstring& candidate) -> bool {
        std::error_code ec;
        bool ok = fs::exists(candidate, ec);
        RLOG << "[TEX]   try '" << wlog(candidate) << "' -> "
             << (ok ? "FOUND" : "miss") << std::endl;
        return ok;
    };
    if (!origExt.empty()) {
        std::wstring c = dirWithSlash + tierWithSlash + stem + origExt;
        if (tryPath(c)) return c;
    }
    auto [alts, n] = AltsForExtW(origExtLower);
    for (std::size_t i = 0; i < n; ++i) {
        std::wstring extW = alts[i];
        if (extW == origExtLower) continue;
        std::wstring c = dirWithSlash + tierWithSlash + stem + extW;
        if (tryPath(c)) return c;
    }
    return {};
}

} // anonymous

// ============================================================================
// Local-disk-only resolver (free function for use outside TextureResolver)
// ============================================================================

std::wstring resolveTexturePath(const std::wstring& modelDir,
                                const std::wstring& relPath) {
    if (relPath.empty()) return {};

    // Normalize separators, trim trailing whitespace/nulls, strip leading
    // slashes so `\Textures\x.blp` doesn't look absolute.
    std::wstring normRel = relPath;
    std::replace(normRel.begin(), normRel.end(), L'/', L'\\');
    while (!normRel.empty() && (normRel.back() <= L' ' || normRel.back() == L'\0'))
        normRel.pop_back();
    while (!normRel.empty() && (normRel.front() == L'\\' || normRel.front() == L'/'))
        normRel.erase(normRel.begin());
    if (normRel.empty()) return {};

    std::wstring dir = modelDir;
    std::replace(dir.begin(), dir.end(), L'/', L'\\');
    if (!dir.empty() && dir.back() != L'\\')
        dir.push_back(L'\\');

    std::wstring relDir, fileName;
    {
        auto lastSep = normRel.find_last_of(L'\\');
        if (lastSep != std::wstring::npos) {
            relDir = normRel.substr(0, lastSep + 1);
            fileName = normRel.substr(lastSep + 1);
        } else {
            fileName = normRel;
        }
    }

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

    RLOG << "[TEX] resolveTexturePath dir='" << wlog(dir)
         << "' rel='" << wlog(normRel) << "'" << std::endl;

    // Peel the relative path one directory at a time. At each tier exhaust
    // every extension (MDX-stated first, then aliases) before dropping a
    // segment. Catches the case where MDX says `path1\path2\Foo.blp` but the
    // texture sits at `<modelDir>\path2\Foo.tif` or `<modelDir>\Foo.tif`.
    std::wstring tier = relDir;
    while (true) {
        if (auto hit = tryDiskAtTier(dir, tier, stem, origExt, origExtLower); !hit.empty())
            return hit;
        if (tier.empty()) break;
        auto sep = tier.find(L'\\');
        tier = (sep == std::wstring::npos) ? std::wstring{} : tier.substr(sep + 1);
    }

    // Fallback: the original MDX path under modelDir, may not exist. The
    // TextureResolver caller checks fs::exists before using it; the legacy
    // buildStdFallback path uses it as a best-guess for BitmapTex.SetMapName.
    std::wstring fallback = dir + relDir + stem + (origExt.empty() ? L".dds" : origExt);
    RLOG << "[TEX]   fallback '" << wlog(fallback) << "'" << std::endl;
    return fallback;
}

// ============================================================================
// TextureResolver — disk + CASC + MPQ unified
// ============================================================================

struct TextureResolver::Impl {
    std::wstring modelDir;

#if defined(WHITEOUT_HAS_CASC)
    std::optional<whiteout::storages::casc::Storage> casc;
#endif
    std::vector<whiteout::storages::mpq::Storage> mpqArchives;
};

namespace {

#if defined(WHITEOUT_HAS_CASC)
std::optional<whiteout::storages::casc::Storage>
openCascImpl(const std::wstring& explicitDir) {
    std::string path;
    if (!explicitDir.empty()) {
        path.reserve(explicitDir.size());
        for (wchar_t c : explicitDir) path += static_cast<char>(c);
        RLOG << "[CASC] Using explicit dir: '" << path << "'" << std::endl;
    } else {
        // Mirror WhiteoutFlakes' detection: prefer an install whose `Data`
        // subdirectory exists (a real WC3 / Reforged install). Falls back
        // to the first hit if nothing has Data — better than silently
        // doing nothing on a portable install.
        RLOG << "[CASC] Auto-detecting WC3 install via findBlizzardGames..." << std::endl;
        auto games = whiteout::utils::findBlizzardGames();
        std::string fallback;
        for (const auto& g : games) {
            RLOG << "[CASC]   candidate: '" << g.name << "' -> '" << g.path << "'" << std::endl;
            if (g.game != whiteout::utils::BlizzardGame::WarcraftIII &&
                g.game != whiteout::utils::BlizzardGame::WarcraftIIIReforged)
                continue;
            if (fs::exists(fs::path(g.path) / "Data")) {
                path = g.path;
                break;
            }
            if (fallback.empty())
                fallback = g.path;
        }
        if (path.empty())
            path = std::move(fallback);
    }
    if (path.empty()) {
        RLOG << "[CASC] No WC3 install resolved — CASC disabled for this import" << std::endl;
        RLOG.flush();
        return std::nullopt;
    }

    // Append `\Data` when the path points at the WC3 install root rather
    // than its Data subdir. The docs claim both work, but in practice the
    // casc backend opens reliably only against `<root>\Data` (matches what
    // WhiteoutTex / the TextureBrowser helper feed it). Case-insensitive
    // check on either separator so we don't double-append for paths that
    // already end in Data.
    {
        auto endsWithData = [](const std::string& p) {
            if (p.size() < 4) return false;
            std::string tail = p.substr(p.size() - 4);
            for (auto& c : tail)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (tail != "data") return false;
            if (p.size() == 4) return true;
            char sep = p[p.size() - 5];
            return sep == '\\' || sep == '/';
        };
        if (!endsWithData(path)) {
            // Pick the separator that already dominates the path so we don't
            // mix slashes in the final string (some CASC backends are picky).
            char sep = (path.find('\\') != std::string::npos ||
                        path.find('/') == std::string::npos) ? '\\' : '/';
            if (!path.empty() && (path.back() == '\\' || path.back() == '/'))
                path.pop_back();
            path.push_back(sep);
            path += "Data";
            RLOG << "[CASC]   appended Data subdir -> '" << path << "'" << std::endl;
        }
    }

    RLOG << "[CASC] Opening storage at: '" << path << "'" << std::endl;
    RLOG.flush();

    // Use the &error overload so the failure reason actually lands in the
    // log. The bare open(path) overload returns nullopt with no diagnostic,
    // which is what made every previous round of "CASC is silently broken"
    // impossible to debug.
    std::string error;
    auto storage = whiteout::storages::casc::Storage::open(path, &error);
    if (!storage) {
        RLOG << "[CASC] Open FAILED at '" << path << "': "
             << (error.empty() ? std::string{"(no error message)"} : error) << std::endl;
    } else {
        RLOG << "[CASC] Open OK at '" << path << "'" << std::endl;
    }
    RLOG.flush();
    return storage;
}
#endif

// Open the archives to search, in the order to search them.
//
// A configured load order wins outright over the directory scan. The scan
// cannot express priority — fs::directory_iterator hands back whatever order
// the filesystem feels like, so which copy of a patched texture won was
// already luck — and it cannot reach an archive outside `mpqDir` at all.
std::vector<whiteout::storages::mpq::Storage>
openMpqList(const std::vector<std::wstring>& files) {
    std::vector<whiteout::storages::mpq::Storage> out;
    RLOG << "[MPQ] Opening " << files.size() << " archive(s) from the configured load order"
         << std::endl;
    for (const std::wstring& f : files) {
        std::string error;
        auto storage = whiteout::storages::mpq::Storage::open(
            std::filesystem::path(f).string(), &error);
        if (storage) {
            RLOG << "[MPQ]   opened: '" << wlog(f) << "'" << std::endl;
            out.push_back(std::move(*storage));
        } else {
            RLOG << "[MPQ]   open FAILED for '" << wlog(f) << "': "
                 << (error.empty() ? std::string{"(no error message)"} : error) << std::endl;
        }
    }
    RLOG << "[MPQ] Load order complete — " << out.size() << " of " << files.size()
         << " opened" << std::endl;
    RLOG.flush();
    return out;
}

std::vector<whiteout::storages::mpq::Storage>
openMpqImpl(const std::wstring& mpqDir) {
    std::vector<whiteout::storages::mpq::Storage> out;
    if (mpqDir.empty()) {
        RLOG << "[MPQ] mpqDir empty — MPQ disabled" << std::endl;
        RLOG.flush();
        return out;
    }
    std::error_code ec;
    if (!fs::is_directory(mpqDir, ec)) {
        RLOG << "[MPQ] mpqDir not a directory: '" << wlog(mpqDir) << "'" << std::endl;
        RLOG.flush();
        return out;
    }
    RLOG << "[MPQ] Scanning '" << wlog(mpqDir) << "' for *.mpq..." << std::endl;
    int scanned = 0;
    for (const auto& entry : fs::directory_iterator(mpqDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(::tolower(c)); });
        if (ext != ".mpq") continue;
        ++scanned;
        std::string error;
        auto storage = whiteout::storages::mpq::Storage::open(entry.path().string(), &error);
        if (storage) {
            RLOG << "[MPQ]   opened: '" << wlog(entry.path().wstring()) << "'" << std::endl;
            out.push_back(std::move(*storage));
        } else {
            RLOG << "[MPQ]   open FAILED for '" << wlog(entry.path().wstring()) << "': "
                 << (error.empty() ? std::string{"(no error message)"} : error)
                 << std::endl;
        }
    }
    RLOG << "[MPQ] Scan complete — " << scanned << " .mpq found, "
         << out.size() << " opened" << std::endl;
    RLOG.flush();
    return out;
}

// Lowercase + backslash-normalize an MDX path for CASC/MPQ lookup keys.
std::string normalizeArchivePath(const std::wstring& relPath) {
    std::string out;
    out.reserve(relPath.size());
    for (wchar_t c : relPath) {
        char ch = static_cast<char>(c);
        if (ch == '/') ch = '\\';
        out += static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

// Extract MDX-stated extension (lowercased, with dot). Empty when none.
std::string extractMdxExt(const std::string& archiveRel) {
    auto dot = archiveRel.find_last_of('.');
    if (dot == std::string::npos) return {};
    return archiveRel.substr(dot);
}

// Build a per-call extension order: MDX-stated extension first, then every
// alias from the per-purpose set, skipping the MDX one to avoid a redundant
// retry. An unknown MDX extension yields a single-element list (no aliases).
std::vector<std::string> buildOrderedExts(const std::string& mdxExt) {
    std::vector<std::string> out;
    auto [alts, n] = AltsForExtA(mdxExt);
    out.reserve(n + 1);
    if (!mdxExt.empty()) out.push_back(mdxExt);
    for (std::size_t i = 0; i < n; ++i) {
        const char* ext = alts[i];
        if (ext != mdxExt) out.emplace_back(ext);
    }
    return out;
}

bool writeBytesToDisk(const fs::path& outPath, const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    fs::create_directories(outPath.parent_path(), ec);
    std::ofstream ofs(outPath, std::ios::binary);
    if (!ofs) {
        RLOG << "[EXT]   write open failed: " << wlog(outPath.wstring()) << std::endl;
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    ofs.close();
    if (!ofs.good()) {
        RLOG << "[EXT]   write failed: " << wlog(outPath.wstring()) << std::endl;
        return false;
    }
    return true;
}

} // anonymous

TextureResolver::TextureResolver(const std::wstring& modelDir,
                                 const std::wstring& cascDir,
                                 const std::wstring& mpqDir,
                                 const std::vector<std::wstring>& mpqArchives)
    : impl_(std::make_unique<Impl>()) {
    impl_->modelDir = modelDir;

    RLOG << "[RES] TextureResolver ctor:"
         << " modelDir='" << wlog(modelDir) << "'"
         << " cascDir='" << wlog(cascDir) << "'"
         << " mpqDir='" << wlog(mpqDir) << "'"
         << " listed=" << mpqArchives.size() << std::endl;
    RLOG.flush();

#if defined(WHITEOUT_HAS_CASC)
    impl_->casc = openCascImpl(cascDir);
#else
    (void)cascDir;
#endif
    impl_->mpqArchives =
        mpqArchives.empty() ? openMpqImpl(mpqDir) : openMpqList(mpqArchives);
    RLOG.flush();
}

TextureResolver::~TextureResolver() = default;

bool TextureResolver::HasCasc() const {
#if defined(WHITEOUT_HAS_CASC)
    return impl_->casc.has_value();
#else
    return false;
#endif
}

bool TextureResolver::HasMpq() const {
    return !impl_->mpqArchives.empty();
}

std::wstring TextureResolver::Resolve(const std::wstring& relPath) {
    if (relPath.empty()) return {};

    RLOG << "[RES] Resolve('" << wlog(relPath) << "')"
         << " casc=" << (impl_->casc.has_value() ? "open" : "closed")
         << " mpq=" << impl_->mpqArchives.size() << " archives"
         << std::endl;

    // 1) Local disk first (with subdir peeling + alias fallback). When the
    //    resolved path actually exists, return it verbatim — no archive
    //    extraction needed.
    {
        std::wstring local = resolveTexturePath(impl_->modelDir, relPath);
        std::error_code ec;
        if (!local.empty() && fs::exists(local, ec)) {
            RLOG << "[RES] disk hit: '" << wlog(local) << "'" << std::endl;
            RLOG.flush();
            return local;
        }
    }

    // Common archive lookup state.
    std::string archiveRel = normalizeArchivePath(relPath);
    std::string mdxExt = extractMdxExt(archiveRel);
    std::string archiveStem = mdxExt.empty() ? archiveRel
                                              : archiveRel.substr(0, archiveRel.size() - mdxExt.size());
    auto orderedExts = buildOrderedExts(mdxExt);

#if defined(WHITEOUT_HAS_CASC)
    // 2) CASC. SD prefix first, then HD, then the deprecated bucket (some
    //    assets were moved out of the main war3.w3mod tree across patches
    //    and only live in the deprecated overlay now). MDX-stated extension
    //    is tried before any alias inside each prefix so a real MDX `.tif`
    //    request that has a CASC `.dds` lands extracted as `.dds` only
    //    when no `.tif` lives in CASC. Matches WhiteoutFlakes' lookup order.
    if (impl_->casc) {
        static const char* kPrefixes[] = {
            "war3.w3mod:",
            "war3.w3mod:_hd.w3mod:",
            "war3.w3mod:_deprecated.w3mod:",
        };
        RLOG << "[CASC] Searching for: '" << archiveStem
             << "' (mdxExt='" << mdxExt << "')" << std::endl;
        for (const char* prefix : kPrefixes) {
            for (const std::string& ext : orderedExts) {
                std::string cascPath = std::string(prefix) + archiveStem + ext;
                auto data = impl_->casc->readFile(cascPath);
                if (!data || data->empty()) continue;
                RLOG << "[CASC] Found: '" << cascPath << "' (" << data->size() << " bytes)" << std::endl;

                // Write to <modelDir>/<original-subdir>/<stem>.<actualExt>.
                std::wstring outRel = swapExtension(relPath, narrowToWide(ext));
                fs::path outPath = fs::path(impl_->modelDir) / outRel;
                std::error_code ec;
                if (fs::exists(outPath, ec)) {
                    RLOG << "[CASC]   already on disk: " << wlog(outPath.wstring()) << std::endl;
                    return outPath.wstring();
                }
                if (writeBytesToDisk(outPath, *data)) {
                    RLOG << "[CASC]   extracted to: " << wlog(outPath.wstring()) << std::endl;
                    return outPath.wstring();
                }
            }
        }
    }
#endif

    // 3) MPQ. Each archive in load order; MDX extension first, then aliases.
    if (!impl_->mpqArchives.empty()) {
        RLOG << "[MPQ] Searching for: " << archiveStem
             << " (mdxExt='" << mdxExt << "')" << std::endl;
        for (const auto& storage : impl_->mpqArchives) {
            if (!storage) continue;
            for (const std::string& ext : orderedExts) {
                std::string mpqPath = archiveStem + ext;
                auto data = storage.readFile(mpqPath);
                if (!data || data->empty()) continue;
                RLOG << "[MPQ] Found: " << mpqPath << " (" << data->size() << " bytes)" << std::endl;

                std::wstring outRel = swapExtension(relPath, narrowToWide(ext));
                fs::path outPath = fs::path(impl_->modelDir) / outRel;
                std::error_code ec;
                if (fs::exists(outPath, ec)) {
                    RLOG << "[MPQ]   already on disk: " << wlog(outPath.wstring()) << std::endl;
                    return outPath.wstring();
                }
                if (writeBytesToDisk(outPath, *data)) {
                    RLOG << "[MPQ]   extracted to: " << wlog(outPath.wstring()) << std::endl;
                    return outPath.wstring();
                }
            }
        }
    }

    // 4) Nothing — fallback to the local-disk best-guess (MDX path under
    //    modelDir with the MDX extension). BitmapTex displays it so the
    //    artist can hand-edit.
    RLOG << "[RES] MISS for '" << wlog(relPath) << "' — no source had it"
         << " (casc=" << (impl_->casc.has_value() ? "open" : "closed")
         << ", mpq=" << impl_->mpqArchives.size() << ")" << std::endl;
    RLOG.flush();
    auto fallback = resolveTexturePath(impl_->modelDir, relPath);
    RLOG << "[RES] miss; fallback: '" << wlog(fallback) << "'" << std::endl;
    return fallback;
}

} // namespace mdx_scene
