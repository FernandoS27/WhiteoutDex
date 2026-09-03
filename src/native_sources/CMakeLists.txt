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
option(BUILD_COMMON_ONLY "Build only Max-SDK-independent libraries (whiteout_lib, zlib, WhiteoutDexTextureBrowser)" OFF)
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
    message(STATUS "Using pre-built common libs from: ${PREBUILT_COMMON_DIR}")

    add_library(whiteout_lib STATIC IMPORTED GLOBAL)
    set_target_properties(whiteout_lib PROPERTIES
        IMPORTED_LOCATION "${PREBUILT_COMMON_DIR}/WhiteoutLib/Release/whiteout_lib.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${WHITEOUT_DIR}/include"
    )

    if(EXISTS "${PREBUILT_COMMON_DIR}/WhiteoutLib/Release/whiteout_casc.lib")
        add_library(whiteout_casc STATIC IMPORTED GLOBAL)
        set_target_properties(whiteout_casc PROPERTIES
            IMPORTED_LOCATION "${PREBUILT_COMMON_DIR}/WhiteoutLib/Release/whiteout_casc.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${WHITEOUT_DIR}/include"
            INTERFACE_LINK_LIBRARIES whiteout_lib
            INTERFACE_COMPILE_DEFINITIONS "WHITEOUT_HAS_CASC=1"
        )
    endif()

    if(EXISTS "${PREBUILT_COMMON_DIR}/WhiteoutLib/Release/whiteout_database.lib")
        add_library(whiteout_database STATIC IMPORTED GLOBAL)
        set_target_properties(whiteout_database PROPERTIES
            IMPORTED_LOCATION "${PREBUILT_COMMON_DIR}/WhiteoutLib/Release/whiteout_database.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${WHITEOUT_DIR}/include"
            INTERFACE_LINK_LIBRARIES whiteout_lib
            INTERFACE_COMPILE_DEFINITIONS "WHITEOUT_HAS_DATABASE=1"
        )
    endif()

    if(EXISTS "${PREBUILT_COMMON_DIR}/WhiteoutLib/Release/whiteout_mpq.lib")
        add_library(whiteout_mpq STATIC IMPORTED GLOBAL)
        set_target_properties(whiteout_mpq PROPERTIES
            IMPORTED_LOCATION "${PREBUILT_COMMON_DIR}/WhiteoutLib/Release/whiteout_mpq.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${WHITEOUT_DIR}/include"
            INTERFACE_LINK_LIBRARIES whiteout_lib
            INTERFACE_COMPILE_DEFINITIONS "WHITEOUT_HAS_MPQ=1"
        )
    endif()

    # ── zlibstatic (built via FetchContent during BUILD_COMMON_ONLY) ──────
    # FetchContent puts sources in _deps/zlib-src and builds in _deps/zlib-build.
    # The .lib lands at _deps/zlib-build/<Config>/zlibstatic.lib.
    set(_zlib_lib_release "${PREBUILT_COMMON_DIR}/_deps/zlib-build/Release/zlibstatic.lib")
    set(_zlib_lib_debug   "${PREBUILT_COMMON_DIR}/_deps/zlib-build/Debug/zlibstaticd.lib")
    set(_zlib_inc_src     "${PREBUILT_COMMON_DIR}/_deps/zlib-src")
    set(_zlib_inc_bin     "${PREBUILT_COMMON_DIR}/_deps/zlib-build")
    if(EXISTS "${_zlib_lib_release}")
        add_library(zlibstatic STATIC IMPORTED GLOBAL)
        set_target_properties(zlibstatic PROPERTIES
            IMPORTED_LOCATION         "${_zlib_lib_release}"
            IMPORTED_LOCATION_RELEASE "${_zlib_lib_release}"
            INTERFACE_INCLUDE_DIRECTORIES "${_zlib_inc_src};${_zlib_inc_bin}"
        )
        if(EXISTS "${_zlib_lib_debug}")
            set_target_properties(zlibstatic PROPERTIES
                IMPORTED_LOCATION_DEBUG "${_zlib_lib_debug}")
        endif()
        message(STATUS "Imported zlibstatic: ${_zlib_lib_release}")
    else()
        message(WARNING "zlibstatic not found at ${_zlib_lib_release} — MPQ support will not work")
    endif()

    # ── WhiteoutDexTextureBrowser_native (output dir = build_common root) ──
    # The TextureBrowser CMakeLists sets ARCHIVE_OUTPUT_DIRECTORY = CMAKE_BINARY_DIR,
    # so the .lib lands at <build_common>/<Config>/WhiteoutDexTextureBrowser_native.lib.
    set(_tb_lib_release "${PREBUILT_COMMON_DIR}/Release/WhiteoutDexTextureBrowser_native.lib")
    set(_tb_lib_debug   "${PREBUILT_COMMON_DIR}/Debug/WhiteoutDexTextureBrowser_native.lib")
    set(_tb_src_dir     "${CMAKE_CURRENT_SOURCE_DIR}/WhiteoutDexTextureBrowser/Source")
    if(EXISTS "${_tb_lib_release}")
        add_library(WhiteoutDexTextureBrowser_native STATIC IMPORTED GLOBAL)
        set_target_properties(WhiteoutDexTextureBrowser_native PROPERTIES
            IMPORTED_LOCATION         "${_tb_lib_release}"
            IMPORTED_LOCATION_RELEASE "${_tb_lib_release}"
            INTERFACE_INCLUDE_DIRECTORIES "${_tb_src_dir}"
        )
        if(EXISTS "${_tb_lib_debug}")
            set_target_properties(WhiteoutDexTextureBrowser_native PROPERTIES
                IMPORTED_LOCATION_DEBUG "${_tb_lib_debug}")
        endif()
        # Forward transitive deps that the .lib was originally built with.
        set(_tb_deps whiteout_lib)
        if(TARGET zlibstatic)
            list(APPEND _tb_deps zlibstatic)
        endif()
        if(TARGET whiteout_casc)
            list(APPEND _tb_deps whiteout_casc)
        endif()
        if(TARGET whiteout_mpq)
            list(APPEND _tb_deps whiteout_mpq)
        endif()
        set_target_properties(WhiteoutDexTextureBrowser_native PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_tb_deps}")
        message(STATUS "Imported WhiteoutDexTextureBrowser_native: ${_tb_lib_release}")
    else()
        message(WARNING "WhiteoutDexTextureBrowser_native not found at ${_tb_lib_release} — MPQ support will not work")
    endif()

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
# ===========================================================================
if(NOT BUILD_COMMON_ONLY)
    set(WDX_BUILD_MAX_PLUGIN OFF CACHE BOOL "" FORCE)

    set(WHITEOUTFLAKES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../external/WhiteoutFlakes"
        CACHE PATH "WhiteoutFlakes root")
    if(NOT EXISTS "${WHITEOUTFLAKES_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "WhiteoutFlakes not found at: ${WHITEOUTFLAKES_DIR}\n"
            "Run: git submodule update --init --recursive external/WhiteoutFlakes")
    endif()

    add_subdirectory("${WHITEOUTFLAKES_DIR}" "${CMAKE_BINARY_DIR}/WhiteoutFlakes")
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
#   standalone→ Standalone/*               (exe + shaders + DLLs)
#   scripts   → scripts/{macroscripts,pre_startup_scripts,…}/*
#   manifest  → PackageContents.xml
#
# Typical invocations:
#   cmake --install <build> --prefix output                                 (suite layout)
#   cmake --install <build> --prefix <AppData>/…/WhiteoutDex/native plugins \
#         --component plugins                                               (AppData plugins)
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

    # Ship the prebuilt BLS shader bundles next to the .dlx so the renderer's
    # FileContentProvider — with the install path overridden to the .dlx
    # directory in ndxStart() — finds them. Without this, ndxStart hits
    # "shader bundle not found" the moment it tries to compile a permute.
    set(_wdx_prebuilt_shaders "${WHITEOUTFLAKES_DIR}/prebuilt/shaders")
    if(EXISTS "${_wdx_prebuilt_shaders}")
        install(DIRECTORY "${_wdx_prebuilt_shaders}/"
            DESTINATION "${WDX_PLUGIN_DEST}/shaders"
            COMPONENT plugins
        )
    endif()

    # Standalone viewer ships as a folder (exe + shaders/ + DLLs). The
    # WhiteoutFlakes subproject writes the whole tree to <build>/standalone,
    # so copy it wholesale rather than reasoning about individual targets.
    if(EXISTS "${CMAKE_BINARY_DIR}/standalone" OR TARGET WhiteoutFlakesStandalone)
        install(DIRECTORY "${CMAKE_BINARY_DIR}/standalone/"
            DESTINATION "Standalone"
            COMPONENT standalone
            USE_SOURCE_PERMISSIONS
            OPTIONAL
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

    # PackageContents.xml is generated by the root CMakeLists (per
    # WDX_MAX_VERSIONS) and consumed directly by the installer .iss — no
    # sub-build install rule needed here.
endif()
