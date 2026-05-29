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
#include <sstream>
#include <string>

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

std::optional<wt::Texture> loadSource(const std::string& srcPath) {
    const std::string ext = lowerExt(srcPath);
    if (ext == ".blp") {
        blp::Parser p;
        return p.parse(srcPath);
    }
    if (ext == ".dds") {
        dds::Parser p;
        return p.parse(srcPath);
    }
    if (ext == ".png") {
        png::Parser p;
        return p.parse(srcPath);
    }
    if (ext == ".tga") {
        tga::Parser p;
        return p.parse(srcPath);
    }
    if (ext == ".bmp") {
        bmp::Parser p;
        return p.parse(srcPath);
    }
    if (ext == ".jpg" || ext == ".jpeg") {
        jpg::Parser p;
        return p.parse(srcPath);
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

bool writeBlp(const wt::Texture& texIn, const std::string& dstPath,
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
    writer.write(dstPath, tex, sopt);
    // Lenient mode reports "issues" for warnings too; treat the write as
    // successful whenever the file is on disk afterwards.
    if (writer.hasIssues()) {
        for (const auto& m : writer.getIssues())
            ELOG << "    [BLP issue] " << m << "\n";
    }
    return fs::exists(dstPath);
}

bool writeDds(const wt::Texture& texIn, const std::string& dstPath,
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
    writer.write(dstPath, tex);
    if (writer.hasIssues()) {
        for (const auto& m : writer.getIssues())
            ELOG << "    [DDS issue] " << m << "\n";
    }
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

    const bool isReforged = (opts.version >= 1200);
    const char* targetExt = isReforged ? ".dds" : ".blp";

    fs::path mdxDir = fs::path(widen(mdxOutputPath)).parent_path();
    ELOG << "[texconv] target=" << (isReforged ? "DDS" : "BLP")
         << " textures=" << model.textures.size()
         << " mdxDir=" << mdxDir.string() << "\n";

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

        // Verify source exists before doing anything. If the bitmap was
        // never resolved on disk, skip silently — the user will see the
        // unconverted reference in the MDX which is still valid.
        std::error_code ec;
        if (!fs::exists(widen(tex.sourceDiskPath), ec)) {
            ELOG << "    skip: source not on disk\n";
            reporter.warning(
                L"Texture source not found: " + widen(tex.sourceDiskPath));
            continue;
        }

        // Compute MDX-relative path with the target extension, and the
        // matching on-disk destination next to the .mdx file.
        std::string mdxRel = swapExt(tex.filePath, targetExt);

        // The MDX-relative path uses backslashes; build a filesystem path
        // that mirrors that structure under mdxDir.
        std::string mdxRelFwd = mdxRel;
        std::replace(mdxRelFwd.begin(), mdxRelFwd.end(), '\\', '/');
        fs::path dstPath = mdxDir / widen(mdxRelFwd);
        dstPath = dstPath.make_preferred();
        ELOG << "    dst=" << dstPath.string() << "\n";

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

        // Encode + write.
        const std::string dstUtf8 = dstPath.string();
        const ir::TextureSlot slot = slotByTex[i];
        const bool ok = isReforged
                            ? writeDds(*loaded, dstUtf8, opts, slot)
                            : writeBlp(*loaded, dstUtf8, opts);

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
