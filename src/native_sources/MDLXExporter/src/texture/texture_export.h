// MDLXExporter — Texture conversion at export time
//
// Reads each ir::Texture's sourceDiskPath, decodes it through WhiteoutLib's
// format parsers, optionally regenerates mipmaps, encodes it as the target
// format (BLP for v800, DDS for v1200) per the user's options, and writes
// it next to the .mdx file. Updates ir::Texture::filePath so the in-MDX
// reference matches the converted file's extension.
#pragma once

#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include "../mdx_export_options.h"

#include <string>

namespace mdx_export {

// Convert all textures with a non-empty sourceDiskPath. mdxOutputPath is the
// final .mdx file path; the texture output directory is its parent. Skips
// textures where the destination already exists (unless overwrite is on).
// Errors and per-texture status go through the reporter.
void convertExportTextures(ir::IRModel& model,
                           const std::string& mdxOutputPath,
                           const MdxExportOptions& opts,
                           core::ExportErrorReporter& reporter);

// Save one texture file under another name, for the Material Editor. The target
// extension picks the format: .blp .dds .png .tga .bmp .jpg. Same format means
// a plain copy, so nothing is lost. BLP and DDS are encoded as the export
// settings in `opts` say, with a full mip chain; normalMap selects BC5 for DDS.
// Returns an empty string when saved, otherwise the reason (English, from the
// library or a file error).
std::wstring saveTextureAs(const std::wstring& source, const std::wstring& target,
                           const MdxExportOptions& opts, bool normalMap);

} // namespace mdx_export
