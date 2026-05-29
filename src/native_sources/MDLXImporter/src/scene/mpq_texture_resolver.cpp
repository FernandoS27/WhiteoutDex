// MDLXImporter — MPQ texture extraction (Classic v800 archives)
//
// Backed by WhiteoutLib's mpq::Storage. Mirrors the CASC resolver pattern in
// wc3_material_builder.cpp.

#ifndef WHITEOUT_HAS_MPQ
#define WHITEOUT_HAS_MPQ 1
#endif

#include "texture_resolver.h"

#include <whiteout/storages/mpq/storage.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace mdx_scene {
namespace {

namespace fs  = std::filesystem;
namespace mpq = whiteout::storages::mpq;

struct MpqArchives {
    std::vector<mpq::Storage> archives;
};

std::wstring widen(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (unsigned char c : s) w += static_cast<wchar_t>(c);
    return w;
}

} // namespace

void* openMpqStorage(const std::wstring& mpqDir) {
    if (mpqDir.empty()) return nullptr;
    std::error_code ec;
    if (!fs::is_directory(mpqDir, ec)) return nullptr;

    auto holder = std::make_unique<MpqArchives>();
    for (const auto& entry : fs::directory_iterator(mpqDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return static_cast<char>(::tolower(c)); });
        if (ext != ".mpq") continue;

        auto storage = mpq::Storage::open(entry.path().string());
        if (storage) holder->archives.push_back(std::move(*storage));
    }
    if (holder->archives.empty()) return nullptr;
    return holder.release();
}

void closeMpqStorage(void* storage) {
    delete static_cast<MpqArchives*>(storage);
}

std::wstring extractTextureFromMPQ(
    void* mpqStorage,
    const std::wstring& modelDir,
    const std::wstring& relPath)
{
    if (!mpqStorage || relPath.empty()) return {};
    auto* archives = static_cast<MpqArchives*>(mpqStorage);

    // Convert wide path to ASCII; MPQ uses backslashes.
    std::string mpqRel;
    mpqRel.reserve(relPath.size());
    for (wchar_t c : relPath) {
        char ch = static_cast<char>(c);
        if (ch == '/') ch = '\\';
        mpqRel += ch;
    }

    // Stem (no extension) — we'll try common ones if the as-is path misses.
    std::string mpqStem = mpqRel;
    if (auto dot = mpqStem.rfind('.'); dot != std::string::npos)
        mpqStem.resize(dot);

    // Candidates: as-is first, then common texture extensions.
    std::vector<std::string> candidates;
    candidates.reserve(4);
    candidates.push_back(mpqRel);
    for (const char* ext : { ".blp", ".dds", ".tga" }) {
        std::string c = mpqStem + ext;
        if (c != mpqRel) candidates.push_back(std::move(c));
    }

    for (const auto& storage : archives->archives) {
        if (!storage) continue;
        for (const auto& mpqPath : candidates) {
            auto data = storage.readFile(mpqPath);
            if (!data || data->empty()) continue;

            // Build local output path: modelDir + relative path with the
            // extension we actually found.
            std::string outRel = mpqPath;
            std::replace(outRel.begin(), outRel.end(), '\\', '/');
            fs::path outPath = fs::path(modelDir) / widen(outRel);
            outPath = outPath.make_preferred();

            std::error_code ec;
            fs::create_directories(outPath.parent_path(), ec);

            std::ofstream ofs(outPath, std::ios::binary);
            if (!ofs) continue;
            ofs.write(reinterpret_cast<const char*>(data->data()),
                      static_cast<std::streamsize>(data->size()));
            ofs.close();
            if (ofs.good()) return outPath.wstring();
        }
    }
    return {};
}

} // namespace mdx_scene
