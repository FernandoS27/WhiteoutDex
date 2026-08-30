# ============================================================================
# WhiteoutDexRenderer — 3ds Max plug-in (.dlx) hosting the WhiteoutFlakes
# renderer inside Max.
#
# Owned by this repo (not the WhiteoutFlakes submodule's tools/max_plugin/)
# so the .dlx ships under the same build pipeline as MDLXExporter / blp-max:
# parent CMakeLists provides MaxSDK + whiteout_lib + MAX_VERSION; we just
# consume the WhiteoutFlakesLib + cubeb static libraries the submodule builds.
#
# MaxScript primitives exposed: WhiteoutFlakesStart / Stop / PushSequences /
# RefreshMaterials (dllmain.cpp) and the WdxBrowser* storage-browser family
# (storage_browser_primitives.cpp).
# ============================================================================

set(WHITEOUTFLAKES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../external/WhiteoutFlakes")

# Companion sources we keep referencing from the submodule — copying them in
# would duplicate code that's still in active flux upstream.
set(_basic_viewer_dir "${WHITEOUTFLAKES_DIR}/tools/basic_viewer")
set(_tools_common_dir "${WHITEOUTFLAKES_DIR}/tools/common")
set(_imgui_backends   "${WHITEOUTFLAKES_DIR}/externals/imgui/backends")

add_library(WhiteoutDexRenderer SHARED
    dllmain.cpp
    max_scene_adapter.cpp
    render_window.cpp
    max_plugin_ui.cpp
    # The asset picker: a modal window hosting WhiteoutFlakes' StorageExplorer
    # (grid/tree CASC browser with live model thumbnails), plus the MaxScript
    # primitives that drive it and the headless io::StorageBrowser listing API.
    # Both live here rather than in their own plug-in because this .dlx is
    # already the one linking WhiteoutFlakesLib.
    asset_picker_window.cpp
    storage_browser_primitives.cpp
    resource.rc
    WhiteoutDexExtractor.def

    # Submodule-side helpers: ImGui's Win32 input bridge, INI loader shared
    # with the standalone viewer, cubeb-backed sound emitter, ImGui host
    # theme (also pulls in the embedded Roboto font header).
    "${_imgui_backends}/imgui_impl_win32.cpp"
    "${_basic_viewer_dir}/settings_ini.cpp"
    # settings_io.cpp split out of settings_ini.cpp upstream: it owns
    # ExecutableDir / SettingsIniPath / AssetDir and the per-game IO path
    # overrides, which settings_ini.cpp, dllmain.cpp and max_plugin_ui.cpp
    # all call into.
    "${_basic_viewer_dir}/settings_io.cpp"
    "${_tools_common_dir}/cubeb_sound_emitter.cpp"
    "${_tools_common_dir}/imgui_theme.cpp"
)

set_target_properties(WhiteoutDexRenderer PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    SUFFIX                ".dlx"
    OUTPUT_NAME           "WhiteoutDexRenderer"
)

target_include_directories(WhiteoutDexRenderer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${_basic_viewer_dir}"
    "${_tools_common_dir}"
    "${WHITEOUTFLAKES_DIR}/externals/dr_libs"
    # asset_manager.h transitively pulls cornflakes interface headers
    # (Effect / PkbReader / IArena …) when ndxStart queries the renderer's
    # AssetManager for the cross-model texture-dedup hook.
    "${WHITEOUTFLAKES_DIR}/src/renderer/corn_effects"
)

target_compile_definitions(WhiteoutDexRenderer PRIVATE
    NDX_MAX_VERSION=${MAX_VERSION}
)

target_link_libraries(WhiteoutDexRenderer PRIVATE
    WhiteoutFlakesLib   # built by add_subdirectory(WhiteoutFlakes) in the parent
    # StorageExplorer + ThumbnailPool — the embeddable CASC browser the asset
    # picker hosts. PUBLIC-exports tools/model_explorer and tools/common, so
    # storage_explorer.h and imgui_theme.h both resolve through it.
    WhiteoutFlakesExplorerLib
    MaxSDK
    cubeb               # also from the WhiteoutFlakes subdir
    core.lib
    maxutil.lib
    geom.lib
    mesh.lib
    bmm.lib
    paramblk2.lib
    maxscrpt.lib
    comdlg32
)

# WhiteoutFlakes generates an embedded Roboto-Medium header consumed by
# tools/common/imgui_theme.cpp; the file lands in
# <WhiteoutFlakes-build>/embedded/ and is produced by the
# WhiteoutFlakesEmbedded custom target.
if(TARGET WhiteoutFlakesEmbedded)
    add_dependencies(WhiteoutDexRenderer WhiteoutFlakesEmbedded)
endif()

if(MSVC)
    target_compile_options(WhiteoutDexRenderer PRIVATE
        /W3 /EHsc
        /wd4100 /wd4127 /wd4244 /wd4267
        # C4535: MaxScript's value_locals.h defines ScopedMaxScriptSEHandler,
        # whose inline ctor calls _set_se_translator — which MSVC only honours
        # under /EHa. Pulled in by the GC-local macros in
        # storage_browser_primitives.cpp. We never instantiate that handler (the
        # scripter engine sets its own translator around our _cf calls), so this
        # is the SDK header warning about itself; switching the whole .dlx to
        # /EHa to silence it would change its exception semantics for real code.
        /wd4535
        /permissive
        /Zc:strictStrings-
    )
endif()
