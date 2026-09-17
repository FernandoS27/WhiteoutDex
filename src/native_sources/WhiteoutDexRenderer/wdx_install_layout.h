// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 WhiteoutDex Contributors

#pragma once

/**
 * @file wdx_install_layout.h
 * @brief Where the installer puts the files this .dlx reads at run time.
 *
 * One installer serves every Max version, so anything that is not built per
 * Max SDK ships exactly once, at the bundle root:
 *
 *   <root>\native plugins\Max<year>\WhiteoutDexRenderer.dlx   (per version)
 *   <root>\shaders\                                           (shared)
 *   <root>\lang\                                              (shared)
 *
 * Max 2016/2017 load the same bundle — only their MaxScripts go through a
 * startup loader — so the layout is the same for every year.
 */

#include "io/file_content_provider.h"

#include <filesystem>

#include <windows.h>

namespace wdx {

/// The WhiteoutDex install root, derived from where this .dlx sits:
/// `<root>\native plugins\Max<year>\WhiteoutDexRenderer.dlx`. Empty when the
/// module path cannot be read.
inline std::filesystem::path InstallRootFromModule(HINSTANCE hInstance) {
    wchar_t buf[MAX_PATH] = {};
    if (::GetModuleFileNameW(hInstance, buf, MAX_PATH) == 0)
        return {};
    // …\native plugins\Max2027\x.dlx -> …\native plugins\Max2027 -> … -> root
    return std::filesystem::path(buf).parent_path().parent_path().parent_path();
}

/// Point @p provider's disk lookups at the engine assets the installer ships —
/// the BLS `shaders/` pack RenderPipeline::InitDevice loads.
///
/// The .dlx directory is the base path and the install root the secondary
/// ("system") one, so the resolver tries `<dlx>\shaders\…` before
/// `<root>\shaders\…`. The installed layout only has the root copy; a
/// hand-copied plug-in folder carrying its own `shaders/` still wins. Both are
/// checked before CASC, which has game-shipped `shaders/` paths of its own.
///
/// This replaces the provider's default secondary path, the running
/// executable's directory — 3dsmax.exe's, which holds nothing of ours.
inline void PointAtInstalledAssets(whiteout::flakes::io::FileContentProvider& provider,
                                   HINSTANCE hInstance) {
    wchar_t buf[MAX_PATH] = {};
    if (::GetModuleFileNameW(hInstance, buf, MAX_PATH) == 0)
        return;
    provider.SetBasePath(std::filesystem::path(buf).parent_path());
    provider.SetSystemBasePath(InstallRootFromModule(hInstance));
}

} // namespace wdx
