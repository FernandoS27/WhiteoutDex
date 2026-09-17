cmake_minimum_required(VERSION 3.20)
project(WhiteoutDexPlugins LANGUAGES C CXX RC)

# Included by path rather than inherited from the root: this directory is also
# the SOURCE_DIR of every per-version ExternalProject, which configures it as a
# top level with no CMAKE_MODULE_PATH of its own.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
include(WdxMaxSdk)

# ===========================================================================
# Options
#   -DMAX_VERSION=<year>         (required unless BUILD_COMMON_ONLY)
#   -DMAXSDK_PATH=<path>        (optional, auto-detected from MAX_VERSION)
#   -DBUILD_COMMON_ONLY=ON      (build only Max-SDK-independent libraries)
#   -DPREBUILT_COMMON_DIR=<dir> (import pre-built common libs, skip rebuild)
# ===========================================================================
option(BUILD_COMMON_ONLY "Build only Max-SDK-independent libraries (whiteout_lib, zlib, WhiteoutDexTextureBrowser, WhiteoutFlakes)" OFF)
set(PREBUILT_COMMON_DIR "" CACHE PATH "Path to a build_common/ tree with pre-built common libraries")

# ===========================================================================
# Max SDK pre-check (skip when building only common libs).
# The MaxSDK INTERFACE target is created by WhiteoutFlakes/tools/max_plugin
# once WDX_BUILD_MAX_PLUGIN=ON; the other Max plugins below reuse it.
# ===========================================================================
if(NOT BUILD_COMMON_ONLY)

if(NOT DEFINED MAX_VERSION)
    message(FATAL_ERROR
        "MAX_VERSION not set.  Pass -DMAX_VERSION=<year> to CMake.\n"
        "Example: cmake -DMAX_VERSION=2025 ..")
endif()

if(NOT MAXSDK_PATH)
    wdx_find_max_sdk(${MAX_VERSION} MAXSDK_PATH)
endif()

if(NOT EXISTS "${MAXSDK_PATH}/include/max.h")
    # Name the product install when there is one: "SDK not found" alongside a
    # perfectly good 3ds Max reads as a broken build system rather than a
    # missing optional download.
    wdx_max_install_dir(${MAX_VERSION} _wdx_max_install)
    if(_wdx_max_install)
        set(_wdx_sdk_hint
            "3ds Max ${MAX_VERSION} is installed at ${_wdx_max_install}, but its "
            "SDK is a separate Autodesk download and isn't on this machine.\n")
    else()
        set(_wdx_sdk_hint "3ds Max ${MAX_VERSION} SDK not found.\n")
    endif()
    message(FATAL_ERROR
        "${_wdx_sdk_hint}"
        "Searched: ${WDX_MAX_SDK_ROOTS};$ENV{ProgramFiles}/Autodesk;"
        "C:/Program Files/Autodesk (each for '3ds Max ${MAX_VERSION} SDK/maxsdk').\n"
        "Override with -DMAXSDK_PATH=<path-to-maxsdk>, or add a search root "
        "with -DWDX_MAX_SDK_ROOTS=<dir>.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "3ds Max 2016+ requires 64-bit builds only")
endif()

message(STATUS "========================================")
message(STATUS " WhiteoutDex Plugins - Max ${MAX_VERSION}")
message(STATUS " SDK: ${MAXSDK_PATH}")
message(STATUS "========================================")

endif() # NOT BUILD_COMMON_ONLY

# ===========================================================================
# Common output directories
#   Plugins (.bmi/.dlo/.dlx) -> <build>/plugins/<config>/
# ===========================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")

# ===========================================================================
# WhiteoutLib (BLP codec + CASC/MPQ storage) — pinned by the top-level
# external/WhiteoutLib submodule. Added BEFORE WhiteoutFlakes so its
# nested copy gets skipped via our `if(NOT TARGET whiteout_lib)` guard
# patch (every consumer links the same whiteout_lib).
# ===========================================================================
set(WHITEOUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../external/WhiteoutLib"
    CACHE PATH "WhiteoutLib root")

if(NOT EXISTS "${WHITEOUT_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "WhiteoutLib not found at: ${WHITEOUT_DIR}")
endif()

if(PREBUILT_COMMON_DIR)
    # Everything Max-SDK-independent — WhiteoutLib, zlib, the texture browser's
    # native half and the WhiteoutFlakes renderer libraries — was compiled once
    # by the common build, which exported the lot (usage requirements included)
    # to WdxCommonTargets.cmake. See "Common-libs export" at the bottom.
    set(_wdx_common_targets "${PREBUILT_COMMON_DIR}/WdxCommonTargets.cmake")
    if(NOT EXISTS "${_wdx_common_targets}")
        message(FATAL_ERROR
            "PREBUILT_COMMON_DIR=${PREBUILT_COMMON_DIR} has no WdxCommonTargets.cmake. "
            "Configure and build that directory with -DBUILD_COMMON_ONLY=ON first, "
            "or leave PREBUILT_COMMON_DIR unset to build everything in this tree.")
    endif()
    message(STATUS "Using pre-built common libs from: ${PREBUILT_COMMON_DIR}")
    include("${_wdx_common_targets}")

    # CMake turns an IMPORTED target's include directories into system includes
    # (/external:I), which MSVC searches only after every plain /I — so the Max
    # SDK's include dir would jump ahead of WhiteoutFlakes' own. The two share
    # header names: WhiteoutFlakes' model_template.h includes "particle.h" and
    # then gets the Max SDK's, whose MaxWindowsVersion.h pins _WIN32_WINNT to
    # 0x0502 on Max 2016 and hides IFileDialog. Keep them plain /I, in link
    # order, exactly as an in-tree add_subdirectory would have. Initialises
    # NO_SYSTEM_FROM_IMPORTED on every plug-in target created below.
    set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
else()
    set(WHITEOUT_ENABLE_CASC     ON  CACHE BOOL "" FORCE)
    set(WHITEOUT_ENABLE_MPQ      ON  CACHE BOOL "" FORCE)
    # WhiteoutFlakes forces WHITEOUT_ENABLE_DATABASE=ON for its own WDX_ENABLE_M2
    # path, but it does that *after* we've already added WhiteoutLib here, so the
    # FORCE lands too late to create the whiteout_database target. Turn it on at
    # the point WhiteoutLib is actually configured.
    set(WHITEOUT_ENABLE_DATABASE ON  CACHE BOOL "" FORCE)
    set(WHITEOUT_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
    set(WHITEOUT_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
    add_subdirectory("${WHITEOUT_DIR}" "${CMAKE_BINARY_DIR}/WhiteoutLib" EXCLUDE_FROM_ALL)

    # zlib (for MPQ decompression in WhiteoutDexTextureBrowser)
    include(FetchContent)
    FetchContent_Declare(zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.1
        GIT_SHALLOW    TRUE
    )
    set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL    ON  CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
    target_include_directories(zlibstatic PUBLIC
        "${zlib_SOURCE_DIR}"
        "${zlib_BINARY_DIR}"
    )

    # WhiteoutDexTextureBrowser (no Max SDK dependency)
    add_subdirectory(WhiteoutDexTextureBrowser)
endif()

# ===========================================================================
# MaxSDK INTERFACE library — central definition so every local Max plug-in
# (and the WhiteoutDexRenderer .dlx) shares one consistent set of include
# paths, link dirs and Win32 preprocessor flags. Previously the WhiteoutFlakes
# submodule's tools/max_plugin/CMakeLists owned this, but we now build the
# renderer plug-in from this repo (see WhiteoutDexRenderer/) so the target
# lives here.
# ===========================================================================
if(NOT BUILD_COMMON_ONLY)
    add_library(MaxSDK INTERFACE)
    target_include_directories(MaxSDK INTERFACE "${MAXSDK_PATH}/include")
    target_link_directories(MaxSDK INTERFACE "${MAXSDK_PATH}/lib/x64/Release")
    target_compile_definitions(MaxSDK INTERFACE
        WIN32 _WIN64 _WINDOWS _USRDLL
        UNICODE _UNICODE
        NOMINMAX WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# ===========================================================================
# WhiteoutFlakes — renderer library consumed by WhiteoutDexRenderer below.
# WDX_BUILD_MAX_PLUGIN=OFF: we build the .dlx ourselves so the plug-in source
# lives in this repo (see src/native_sources/WhiteoutDexRenderer/).
#
# It has no Max SDK dependency, so it is part of the common build: a
# per-version sub-build with PREBUILT_COMMON_DIR imports it instead.
# ===========================================================================
set(WHITEOUTFLAKES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../external/WhiteoutFlakes"
    CACHE PATH "WhiteoutFlakes root")
if(NOT EXISTS "${WHITEOUTFLAKES_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "WhiteoutFlakes not found at: ${WHITEOUTFLAKES_DIR}\n"
        "Run: git submodule update --init --recursive external/WhiteoutFlakes")
endif()

if(NOT PREBUILT_COMMON_DIR)
    set(WDX_BUILD_MAX_PLUGIN OFF CACHE BOOL "" FORCE)

    add_subdirectory("${WHITEOUTFLAKES_DIR}" "${CMAKE_BINARY_DIR}/WhiteoutFlakes")

    # The submodule's own executables — the WhiteoutFlakes viewer, ModelExplorer
    # and two throwaway dev tools. WhiteoutDex ships none of them: the renderer
    # reaches Max only as WhiteoutDexRenderer.dlx, linked against the libraries
    # above. Keep them out of ALL_BUILD so no build compiles them; they stay
    # buildable by name for anyone working on Flakes itself. Their install()
    # rules are Linux/macOS-only, so nothing on Windows asks for them.
    foreach(_wdx_flakes_exe
            WhiteoutFlakesStandalone WhiteoutFlakesExplorer BlsReflect D3ShaderAtlas)
        if(TARGET ${_wdx_flakes_exe})
            set_target_properties(${_wdx_flakes_exe} PROPERTIES
                EXCLUDE_FROM_ALL TRUE
                EXCLUDE_FROM_DEFAULT_BUILD TRUE)
        endif()
    endforeach()
endif()

# ===========================================================================
# Common-libs export (BUILD_COMMON_ONLY)
#
# A multi-version build compiles the Max-SDK-independent libraries once, here,
# and every per-version sub-build links that copy through PREBUILT_COMMON_DIR.
# export(TARGETS) writes IMPORTED definitions — locations, include dirs,
# compile definitions, link interfaces — so the sub-builds see exactly the
# usage requirements an in-tree add_subdirectory would have given them.
#
# export() insists that every target it is handed has its link dependencies
# exported too, so the set is the link closure of the roots, walked here rather
# than listed. Dependencies that are themselves IMPORTED (found packages) can't
# be exported; WdxCommonTargets.cmake re-finds those before including the
# export file.
# ===========================================================================
if(BUILD_COMMON_ONLY)
    set(_wdx_queue "")
    foreach(_t
            whiteout_lib whiteout_casc whiteout_mpq whiteout_database zlibstatic
            WhiteoutDexTextureBrowser_native
            WhiteoutFlakesLib WhiteoutFlakesExplorerLib cubeb)
        if(TARGET ${_t})
            list(APPEND _wdx_queue ${_t})
        endif()
    endforeach()

    set(_wdx_exported "")
    set(_wdx_imported "")
    while(_wdx_queue)
        list(POP_FRONT _wdx_queue _t)
        get_target_property(_aliased ${_t} ALIASED_TARGET)
        if(_aliased)
            set(_t ${_aliased})
        endif()
        if(_t IN_LIST _wdx_exported OR _t IN_LIST _wdx_imported)
            continue()
        endif()
        get_target_property(_imported ${_t} IMPORTED)
        if(_imported)
            list(APPEND _wdx_imported ${_t})
            continue()
        endif()
        list(APPEND _wdx_exported ${_t})

        get_target_property(_type ${_t} TYPE)
        set(_deps "")
        get_target_property(_iface ${_t} INTERFACE_LINK_LIBRARIES)
        if(_iface)
            list(APPEND _deps ${_iface})
        endif()
        if(NOT _type STREQUAL "INTERFACE_LIBRARY")
            get_target_property(_link ${_t} LINK_LIBRARIES)
            if(_link)
                list(APPEND _deps ${_link})
            endif()
        endif()
        foreach(_d IN LISTS _deps)
            # Private deps of a static library arrive as $<LINK_ONLY:…>, and
            # cubeb's bundled speex as $<LINK_ONLY:$<BUILD_INTERFACE:speex>>.
            while(_d MATCHES "^\\$<(LINK_ONLY|BUILD_INTERFACE):(.+)>$")
                set(_d "${CMAKE_MATCH_2}")
            endwhile()
            if(TARGET "${_d}")
                list(APPEND _wdx_queue "${_d}")
            elseif(_d MATCHES "^[A-Za-z0-9_.+-]+::[A-Za-z0-9_.+-]+$"
                   AND NOT _d IN_LIST _wdx_imported)
                # A package's imported target. find_package() declares those
                # in the directory that called it, so from up here they aren't
                # targets at all (Threads::Threads is cubeb's, Vulkan::Vulkan
                # WhiteoutFlakes'). Plain library names (user32.lib) and
                # cross-directory "::@" markers drop out without matching.
                list(APPEND _wdx_imported "${_d}")
            endif()
        endforeach()
    endwhile()

    set(_wdx_common_prelude
        "# Generated by src/native_sources/CMakeLists.txt (BUILD_COMMON_ONLY).\n# Included by per-version sub-builds via PREBUILT_COMMON_DIR.\n")
    foreach(_i IN LISTS _wdx_imported)
        if(_i STREQUAL "Threads::Threads")
            string(APPEND _wdx_common_prelude "find_package(Threads REQUIRED)\n")
        elseif(_i STREQUAL "Vulkan::Vulkan")
            string(APPEND _wdx_common_prelude "find_package(Vulkan REQUIRED)\n")
        else()
            message(FATAL_ERROR
                "The common libraries link the imported target ${_i}, which a "
                "per-version sub-build has no way to recreate. Add the "
                "find_package() call that defines it to the list above.")
        endif()
    endforeach()

    export(TARGETS ${_wdx_exported}
        FILE "${CMAKE_BINARY_DIR}/WdxCommonTargetsExport.cmake")
    file(WRITE "${CMAKE_BINARY_DIR}/WdxCommonTargets.cmake"
        "${_wdx_common_prelude}"
        "include(\"\${CMAKE_CURRENT_LIST_DIR}/WdxCommonTargetsExport.cmake\")\n")

    # Several of these are EXCLUDE_FROM_ALL where they are declared (WhiteoutLib,
    # cubeb) — nothing in this tree links them, the sub-builds do — so make
    # ALL_BUILD produce every one of them.
    set(_wdx_buildable "")
    foreach(_t IN LISTS _wdx_exported)
        get_target_property(_type ${_t} TYPE)
        if(NOT _type STREQUAL "INTERFACE_LIBRARY")
            list(APPEND _wdx_buildable ${_t})
        endif()
    endforeach()
    add_custom_target(wdx_common_exports ALL)
    add_dependencies(wdx_common_exports ${_wdx_buildable})

    list(LENGTH _wdx_exported _wdx_n)
    message(STATUS "Common libs: exporting ${_wdx_n} targets (re-found packages: ${_wdx_imported})")
endif()

# ===========================================================================
# Max SDK plug-ins (skip in common-only builds).
# WhiteoutDexRenderer must come AFTER WhiteoutFlakes (consumes WhiteoutFlakesLib
# + cubeb from there); the others only depend on MaxSDK + whiteout_lib.
# ===========================================================================
if(NOT BUILD_COMMON_ONLY)
    add_subdirectory(MaxCore)
    add_subdirectory(MDLXExporter)
    add_subdirectory(MDLXImporter)
    add_subdirectory(blp-max)
    add_subdirectory(Wc3Particles1)
    add_subdirectory(Wc3Particles2)
    add_subdirectory(Wc3Ribbon)
    add_subdirectory(WhiteoutDexRenderer)
endif()

# ===========================================================================
# Install rules
#
# Components, each tied to a relative destination so the *same* build dir
# can be installed multiple times with different --prefix values:
#
#   plugins   → Max<year>/*.{dle,dlo,dlx,bmi}
#   shaders   → shaders/*                  (BLS pack, shared by every Max year)
#   scripts   → scripts/{macroscripts,pre_startup_scripts,…}/*
#
# Typical invocations:
#   cmake --install <build> --prefix output                                 (suite layout)
#   cmake --install <build> --prefix <AppData>/…/WhiteoutDex/native plugins \
#         --component plugins                                               (AppData plugins)
#   cmake --install <build> --prefix <AppData>/…/WhiteoutDex \
#         --component shaders                                               (AppData shaders)
#   cmake --install <build> --prefix <AppData>/…/WhiteoutDex \
#         --component scripts                                               (AppData scripts)
# ===========================================================================
if(NOT BUILD_COMMON_ONLY)
    set(WDX_PLUGIN_DEST "Max${MAX_VERSION}")

    install(TARGETS
        MDLXExporter MDLXImporter
        blp Wc3Particles1 Wc3Particles2 Wc3Ribbon
        WhiteoutDexRenderer
        RUNTIME     DESTINATION "${WDX_PLUGIN_DEST}" COMPONENT plugins
        LIBRARY     DESTINATION "${WDX_PLUGIN_DEST}" COMPONENT plugins
    )

    # The prebuilt BLS shader bundles. Nothing in them depends on the Max SDK,
    # so they go to the install root once rather than beside each year's .dlx
    # (they are ~70 MB). The renderer looks beside the .dlx first and at the
    # root second — see wdx_install_layout.h. Without them ndxStart hits
    # "shader bundle not found" the moment it tries to compile a permute.
    set(_wdx_prebuilt_shaders "${WHITEOUTFLAKES_DIR}/prebuilt/shaders")
    if(EXISTS "${_wdx_prebuilt_shaders}")
        install(DIRECTORY "${_wdx_prebuilt_shaders}/"
            DESTINATION "shaders"
            COMPONENT shaders
        )
    endif()

    # MaxScript assets at the repo root (shared across every Max version).
    set(WDX_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    install(DIRECTORY "${WDX_REPO_ROOT}/src/macroscripts/"
        DESTINATION "scripts/macroscripts" COMPONENT scripts
        FILES_MATCHING PATTERN "*.mcr"
    )
    install(DIRECTORY "${WDX_REPO_ROOT}/src/pre_startup_scripts/"
        DESTINATION "scripts/pre_startup_scripts" COMPONENT scripts
        FILES_MATCHING PATTERN "*.ms"
    )
    install(DIRECTORY "${WDX_REPO_ROOT}/src/post_startup_scripts/"
        DESTINATION "scripts/post_startup_scripts" COMPONENT scripts
        FILES_MATCHING PATTERN "*.ms"
    )
    install(DIRECTORY "${WDX_REPO_ROOT}/src/scripted_plugins/"
        DESTINATION "scripts/scripted_plugins" COMPONENT scripts
        FILES_MATCHING PATTERN "*.ms"
    )

    # PackageContents.xml and the Max 2016/2017 startup loader are generated /
    # referenced by the root CMakeLists (per WDX_MAX_VERSIONS) and consumed
    # directly by the installer .iss — no sub-build install rule needed here.
endif()
