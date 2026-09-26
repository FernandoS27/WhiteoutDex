// MDLXExporter — Texture conversion at export time

#include "texture_export.h"
#include "../mdx_export_debug.h"

#include <whiteout/textures/blp/blp.h>
#include <whiteout/textures/dds/dds.h>
#include <whiteout/textures/png/png.h>
#include <whiteout/textures/tga/tga.h>
#include <whiteout/textures/bmp/bmp.h>
#include <whiteout/textures/jpeg/jpeg.h>
#include <whiteout/textures/texture.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace fs  = std::filesystem;
namespace wt  = whiteout::textures;
namespace blp = whiteout::textures::blp;
namespace dds = whiteout::textures::dds;
namespace png = whiteout::textures::png;
namespace tga = whiteout::textures::tga;
namespace bmp = whiteout::textures::bmp;
namespace jpg = whiteout::textures::jpeg;

namespace mdx_export {
namespace {

// ── String helpers ──────────────────────────────────────────────────────────

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring w(static_cast<size_t>(wlen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), wlen);
    return w;
}

// Narrow a wide path for the debug log, ASCII only.
// std::filesystem::path::string() must NEVER be used for this: it uses the
// STL's strict wide→narrow converter, which throws std::system_error ("No
// mapping for the Unicode character exists in the target multi-byte code
// page") for any character the ANSI code page cannot represent — e.g. an
// export target under a CJK folder. Nothing here catches it, so it would
// escape DoExport and surface in Max as an unexpected system exception.
std::string wlog(const std::wstring& ws) {
    std::string s;
    s.reserve(ws.size());
    for (wchar_t c : ws)
        s += (c < 128) ? static_cast<char>(c) : '?';
    return s;
}

std::string wlog(const fs::path& p) { return wlog(p.wstring()); }

// Write an encoded texture to disk through the wide path. The WhiteoutLib
// writers' file overloads take a narrow std::string, which cannot name a
// file under a non-ANSI directory, so we encode to a buffer and do the file
// I/O ourselves — std::ofstream's fs::path overload is wide on Windows.
bool writeBytes(const fs::path& dstPath, const std::vector<whiteout::u8>& bytes) {
    if (bytes.empty()) return false;
    std::ofstream ofs(dstPath, std::ios::binary);
    if (!ofs) return false;
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    ofs.close();
    return ofs.good();
}

std::string lowerExt(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(::tolower(c)); });
    return ext;
}

std::string stripExt(const std::string& path) {
    auto dot = path.rfind('.');
    return (dot == std::string::npos) ? path : path.substr(0, dot);
}

// ── Source loader: dispatch by extension ────────────────────────────────────

// Read a whole file through the wide path. Same reason as writeBytes: the
// parsers' file overloads take a narrow std::string, which cannot name a
// source bitmap sitting under a non-ANSI directory, so the bytes are read
// here and handed to the parsers' span overload instead.
std::optional<std::vector<whiteout::u8>> readAllBytes(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary | std::ios::ate);
    if (!ifs) return std::nullopt;
    const std::streamoff size = ifs.tellg();
    if (size < 0) return std::nullopt;
    ifs.seekg(0, std::ios::beg);
    std::vector<whiteout::u8> bytes(static_cast<size_t>(size));
    if (size > 0 &&
        !ifs.read(reinterpret_cast<char*>(bytes.data()),
                  static_cast<std::streamsize>(size)))
        return std::nullopt;
    return bytes;
}

std::optional<wt::Texture> loadSource(const std::string& srcPath) {
    const std::string ext = lowerExt(srcPath);

    const auto bytes = readAllBytes(fs::path(widen(srcPath)));
    if (!bytes) return std::nullopt;
    const std::span<const whiteout::u8> data(*bytes);

    if (ext == ".blp") {
        blp::Parser p;
        return p.parse(data);
    }
    if (ext == ".dds") {
        dds::Parser p;
        return p.parse(data);
    }
    if (ext == ".png") {
        png::Parser p;
        return p.parse(data);
    }
    if (ext == ".tga") {
        tga::Parser p;
        return p.parse(data);
    }
    if (ext == ".bmp") {
        bmp::Parser p;
        return p.parse(data);
    }
    if (ext == ".jpg" || ext == ".jpeg") {
        jpg::Parser p;
        return p.parse(data);
    }
    return std::nullopt;
}

// ── Mipmap helper: ensure the texture has a full mip chain ──────────────────

void ensureMipChain(wt::Texture& tex) {
    if (tex.mipCount() > 1) return;

    // generateMipmaps requires uncompressed format. Convert to RGBA8 first
    // if the source is BCn.
    bool wasCompressed = false;
    switch (tex.format()) {
    case wt::PixelFormat::BC1: case wt::PixelFormat::BC2:
    case wt::PixelFormat::BC3: case wt::PixelFormat::BC4:
    case wt::PixelFormat::BC5: case wt::PixelFormat::BC6H:
    case wt::PixelFormat::BC7:
        wasCompressed = true;
        tex.format(wt::PixelFormat::RGBA8);
        break;
    default: break;
    }
    (void)wasCompressed;

    tex.generateMipmaps();
}

// ── Encoders ────────────────────────────────────────────────────────────────

bool writeBlp(const wt::Texture& texIn, const fs::path& dstPath,
              const MdxExportOptions& opts)
{
    blp::SaveOptions sopt;
    sopt.version  = blp::BlpVersion::BLP1;            // Classic v800 → BLP1
    sopt.encoding = (opts.blpCompression == 1)
                        ? blp::BlpEncoding::JPEG
                        : blp::BlpEncoding::Palettized;
    sopt.alpha           = blp::BlpAlphaDepth::Eight;
    sopt.jpegQuality     = std::clamp(opts.blpJpegQuality, 1, 100);
    sopt.dither          = (sopt.encoding == blp::BlpEncoding::Palettized)
                                && opts.blpDithering;

    // Palettized BLP only quantizes uncompressed pixel data; ensure RGBA8.
    wt::Texture tex = texIn;
    if (tex.format() != wt::PixelFormat::RGBA8)
        tex.format(wt::PixelFormat::RGBA8);

    blp::Writer writer;
    const auto bytes = writer.write(tex, sopt);
    // Lenient mode reports "issues" for warnings too; treat the write as
    // successful whenever the file is on disk afterwards.
    if (writer.hasIssues()) {
        for (const auto& m : writer.getIssues())
            ELOG << "    [BLP issue] " << m << "\n";
    }
    if (!writeBytes(dstPath, bytes)) return false;
    return fs::exists(dstPath);
}

bool writeDds(const wt::Texture& texIn, const fs::path& dstPath,
              const MdxExportOptions& opts, ir::TextureSlot slot)
{
    // Slot-aware format selection.
    //   Normal  → BC5 (two-channel; engines reconstruct Z from XY).
    //   Diffuse / ORM / Emissive / TeamColor / Environment / Reflection
    //           → user's BC3/BC7 choice (opts.ddsFormat: 0=BC3, 1=BC7).
    wt::PixelFormat target;
    if (slot == ir::TextureSlot::Normal) {
        target = wt::PixelFormat::BC5;
    } else {
        target = (opts.ddsFormat == 1)
                     ? wt::PixelFormat::BC7
                     : wt::PixelFormat::BC3;
    }

    wt::Texture tex = texIn;
    if (tex.format() != target)
        tex.format(target);

    dds::Writer writer;
    const auto bytes = writer.write(tex);
    if (writer.hasIssues()) {
        for (const auto& m : writer.getIssues())
            ELOG << "    [DDS issue] " << m << "\n";
    }
    if (!writeBytes(dstPath, bytes)) return false;
    return fs::exists(dstPath);
}

// ── In-MDX path: swap extension to target format (.blp or .dds) ─────────────

std::string swapExt(const std::string& path, const char* newExt) {
    if (path.empty()) return path;
    return stripExt(path) + newExt;
}

} // namespace

// ============================================================================

void convertExportTextures(ir::IRModel& model,
                           const std::string& mdxOutputPath,
                           const MdxExportOptions& opts,
                           core::ExportErrorReporter& reporter)
{
    if (!opts.texConvertEnabled) {
        ELOG << "[texconv] disabled — skipping\n";
        return;
    }

    const bool isReforged = (opts.version >= 900);
    const char* targetExt = isReforged ? ".dds" : ".blp";

    fs::path mdxDir = fs::path(widen(mdxOutputPath)).parent_path();
    ELOG << "[texconv] target=" << (isReforged ? "DDS" : "BLP")
         << " textures=" << model.textures.size()
         << " mdxDir=" << wlog(mdxDir) << "\n";

    // Build texture-index -> slot map from material layers. A texture can be
    // referenced from multiple slots; we keep the first non-Diffuse hit so
    // Normal/ORM/Emissive/TeamColor get prioritized over a generic Diffuse.
    std::vector<ir::TextureSlot> slotByTex(model.textures.size(),
                                           ir::TextureSlot::Diffuse);
    std::vector<bool>            slotIsSet(model.textures.size(), false);
    for (const auto& mat : model.materials) {
        for (const auto& layer : mat.layers) {
            for (const auto& ref : layer.textureRefs) {
                if (ref.textureIndex < 0 ||
                    static_cast<size_t>(ref.textureIndex) >= slotByTex.size())
                    continue;
                if (!slotIsSet[ref.textureIndex] ||
                    ref.slot != ir::TextureSlot::Diffuse) {
                    slotByTex[ref.textureIndex] = ref.slot;
                    slotIsSet[ref.textureIndex] = true;
                }
            }
        }
    }

    for (size_t i = 0; i < model.textures.size(); ++i) {
        auto& tex = model.textures[i];
        ELOG << "[texconv] tex[" << i << "] mdxPath='" << tex.filePath
             << "' src='" << tex.sourceDiskPath << "'\n";
        if (tex.sourceDiskPath.empty() || tex.filePath.empty()) {
            ELOG << "    skip: empty source or empty mdx path\n";
            continue;
        }

        // Compute MDX-relative path with the target extension, and the
        // matching on-disk destination next to the .mdx file.
        std::string mdxRel = swapExt(tex.filePath, targetExt);

        // The extractor already searched for a missing source (same name,
        // any readable format, near the scene and in Max's map paths). What
        // is still missing cannot be converted, but the model must still
        // name the texture in the game's format: a NeoDex scene points at a
        // decoded .tga copy of an in-game .blp, and the game has no .tga.
        std::error_code ec;
        if (!fs::exists(widen(tex.sourceDiskPath), ec)) {
            ELOG << "    skip: source not on disk; mdx ref -> " << mdxRel << "\n";
            tex.filePath = mdxRel;
            reporter.warning(
                L"Texture source not found, not converted (the model refers to " +
                widen(mdxRel) + L"): " + widen(tex.sourceDiskPath));
            continue;
        }

        // The MDX-relative path uses backslashes; build a filesystem path
        // that mirrors that structure under mdxDir.
        std::string mdxRelFwd = mdxRel;
        std::replace(mdxRelFwd.begin(), mdxRelFwd.end(), '\\', '/');
        fs::path dstPath = mdxDir / widen(mdxRelFwd);
        dstPath = dstPath.make_preferred();
        ELOG << "    dst=" << wlog(dstPath) << "\n";

        // The source may be the destination itself (a scene whose texture
        // was found as the .blp next to the export): re-encoding it would
        // only lose quality.
        if (fs::equivalent(widen(tex.sourceDiskPath), dstPath, ec)) {
            ELOG << "    skip: source is the destination; mdx ref updated\n";
            tex.filePath = mdxRel;
            continue;
        }

        // Skip if destination exists and overwrite is off — but still
        // update the in-MDX path to the converted extension so the model
        // references the file the user already has.
        if (!opts.texOverwriteExisting && fs::exists(dstPath, ec)) {
            ELOG << "    skip: dst exists (overwrite off); mdx ref updated\n";
            tex.filePath = mdxRel;
            continue;
        }

        // Load source.
        auto loaded = loadSource(tex.sourceDiskPath);
        if (!loaded) {
            ELOG << "    FAIL: loadSource returned null (unsupported ext?)\n";
            std::wstringstream ws;
            ws << L"Texture decode failed: " << widen(tex.sourceDiskPath);
            reporter.warning(ws.str());
            continue;
        }
        ELOG << "    loaded: " << loaded->width() << "x" << loaded->height()
             << " mips=" << loaded->mipCount() << "\n";

        // Optional mipmap regeneration.
        if (opts.texGenerateMipmaps)
            ensureMipChain(*loaded);

        // Make sure the destination directory exists.
        fs::create_directories(dstPath.parent_path(), ec);

        // Encode + write. dstPath stays a fs::path (wide on Windows) so an
        // export target under a non-ANSI directory names the file correctly.
        const ir::TextureSlot slot = slotByTex[i];
        const bool ok = isReforged
                            ? writeDds(*loaded, dstPath, opts, slot)
                            : writeBlp(*loaded, dstPath, opts);

        if (!ok) {
            ELOG << "    FAIL: writer did not produce dst file\n";
            reporter.warning(
                L"Texture write failed: " + dstPath.wstring());
            continue;
        }

        ELOG << "    OK: written\n";
        // Update the in-MDX reference to the new extension.
        tex.filePath = mdxRel;
    }
}

} // namespace mdx_export
