cmake_minimum_required(VERSION 3.20)
project(WhiteoutDexPlugins LANGUAGES C CXX RC)

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

if(NOT DEFINED MAXSDK_PATH)
    set(MAXSDK_PATH "C:/Program Files/Autodesk/3ds Max ${MAX_VERSION} SDK/maxsdk")
endif()

if(NOT EXISTS "${MAXSDK_PATH}/include/max.h")
    message(FATAL_ERROR
        "3ds Max ${MAX_VERSION} SDK not found at: ${MAXSDK_PATH}\n"
        "Override with -DMAXSDK_PATH=<path-to-maxsdk>")
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
    set(WHITEOUT_ENABLE_CASC    ON  CACHE BOOL "" FORCE)
    set(WHITEOUT_ENABLE_MPQ     ON  CACHE BOOL "" FORCE)
    set(WHITEOUT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHITEOUT_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
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
# WhiteoutFlakes — self-contained renderer + 3ds Max plugin.
# Pulled in BEFORE the rest of the Max plugins so the WhiteoutFlakes
# max_plugin subdir creates the shared MaxSDK INTERFACE target; the
# other plugins (MaxCore, MDLXExporter, …) reuse it.
# ===========================================================================
if(NOT BUILD_COMMON_ONLY)
    set(WDX_BUILD_MAX_PLUGIN ON CACHE BOOL "" FORCE)
    # MAX_VERSION / MAXSDK_PATH are forwarded via the cache (already set above).

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
# Max SDK plugins (skip in common-only builds). MaxSDK target is now
# defined by WhiteoutFlakes/tools/max_plugin above.
# ===========================================================================
if(NOT BUILD_COMMON_ONLY)
    add_subdirectory(MaxCore)
    add_subdirectory(MDLXExporter)
    add_subdirectory(MDLXImporter)
    add_subdirectory(blp-max)
    add_subdirectory(Wc3Particles1)
    add_subdirectory(Wc3Particles2)
    add_subdirectory(Wc3Ribbon)
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
        RUNTIME     DESTINATION "${WDX_PLUGIN_DEST}" COMPONENT plugins
        LIBRARY     DESTINATION "${WDX_PLUGIN_DEST}" COMPONENT plugins
    )

    if(TARGET WhiteoutFlakesMaxPlugin)
        install(TARGETS WhiteoutFlakesMaxPlugin
            RUNTIME DESTINATION "${WDX_PLUGIN_DEST}" COMPONENT plugins
            LIBRARY DESTINATION "${WDX_PLUGIN_DEST}" COMPONENT plugins
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
