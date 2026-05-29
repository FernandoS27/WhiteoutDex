cmake_minimum_required(VERSION 3.20)

# ============================================================================
# WhiteoutDex — root build entry point
#
# Single-version mode (focused dev build, opens one .sln in VS):
#   cmake -S . -B build -DMAX_VERSION=2026
#   cmake --build build --config Release --parallel
#   cmake --install build --prefix output                            # suite
#   cmake --install build --prefix "%APPDATA%/Autodesk/ApplicationPlugins/WhiteoutDex/native plugins" --component plugins
#
# Multi-version mode (every installed Max SDK + managed wrappers; CI mode):
#   cmake -S . -B build
#   cmake --build build --config Release --parallel
#   cmake --build build --target deploy_appdata     # → %APPDATA%/…/WhiteoutDex/
#
# Both modes share the per-version logic in src/native_sources/CMakeLists.txt.
# ============================================================================

# ----------------------------------------------------------------------------
# Single-version delegation: defer to src/native_sources/ when MAX_VERSION (or
# BUILD_COMMON_ONLY) is set at the top level. Languages are enabled by the
# subdirectory's own project() call.
# ----------------------------------------------------------------------------
if(DEFINED MAX_VERSION OR BUILD_COMMON_ONLY)
    project(WhiteoutDex LANGUAGES C CXX RC)
    add_subdirectory(src/native_sources)
    return()
endif()

# ----------------------------------------------------------------------------
# Multi-version orchestrator: spawns one ExternalProject per installed Max SDK
# plus the common-libs base sub-build, then drives the managed wrappers and
# deployment targets. No C/CXX languages at this level — pure scheduling.
# ----------------------------------------------------------------------------
project(WhiteoutDex LANGUAGES NONE)

include(ExternalProject)

# ----------------------------------------------------------------------------
# Detect installed Max SDKs (matches Build.bat's probe). Caller can override
# with -DWDX_MAX_VERSIONS="2025;2026" to limit the set.
# ----------------------------------------------------------------------------
set(_wdx_detected "")
foreach(v 2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027)
    if(EXISTS "C:/Program Files/Autodesk/3ds Max ${v} SDK/maxsdk/include/max.h")
        list(APPEND _wdx_detected "${v}")
    endif()
endforeach()

set(WDX_MAX_VERSIONS "${_wdx_detected}" CACHE STRING
    "Semicolon-separated Max versions to build (default: every installed SDK)")

if(NOT WDX_MAX_VERSIONS)
    message(FATAL_ERROR
        "No 3ds Max SDKs detected.\n"
        "Install a Max SDK, or use single-version mode: cmake -DMAX_VERSION=<year>.")
endif()

message(STATUS "WhiteoutDex: multi-version build for Max ${WDX_MAX_VERSIONS}")

# ----------------------------------------------------------------------------
# Forward the parent generator + arch to every sub-build so the IDE picks up
# matching .vcxproj files across all per-version build dirs.
# ----------------------------------------------------------------------------
set(_sub_gen_args -G "${CMAKE_GENERATOR}")
if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND _sub_gen_args -A "${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND _sub_gen_args -T "${CMAKE_GENERATOR_TOOLSET}")
endif()

set(WDX_STAGE_DIR "${CMAKE_BINARY_DIR}/stage")

# ----------------------------------------------------------------------------
# Common-libs sub-build — whiteout_lib + zlib + WhiteoutDexTextureBrowser
# native static lib. Shared by every per-version build via PREBUILT_COMMON_DIR.
# ----------------------------------------------------------------------------
ExternalProject_Add(common_libs
    SOURCE_DIR        "${CMAKE_SOURCE_DIR}/src/native_sources"
    BINARY_DIR        "${CMAKE_BINARY_DIR}/common"
    CONFIGURE_COMMAND "${CMAKE_COMMAND}" ${_sub_gen_args} -DBUILD_COMMON_ONLY=ON
                      "<SOURCE_DIR>"
    BUILD_COMMAND     "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release
    INSTALL_COMMAND   ""
    BUILD_ALWAYS      TRUE
)

# ----------------------------------------------------------------------------
# Per-Max-version sub-build. Each installs to ${WDX_STAGE_DIR}, producing:
#   stage/Max<year>/*.{dle,dlo,dlx,bmi}
#   stage/Standalone/*                (shared; last writer wins, contents same)
#   stage/scripts/{macroscripts,…}/*
#   stage/PackageContents.xml
# ----------------------------------------------------------------------------
set(_per_version_targets "")
foreach(v IN LISTS WDX_MAX_VERSIONS)
    set(_tgt plugins_Max${v})
    ExternalProject_Add(${_tgt}
        SOURCE_DIR        "${CMAKE_SOURCE_DIR}/src/native_sources"
        BINARY_DIR        "${CMAKE_BINARY_DIR}/build_${v}"
        CONFIGURE_COMMAND "${CMAKE_COMMAND}" ${_sub_gen_args}
                          -DMAX_VERSION=${v}
                          -DPREBUILT_COMMON_DIR=${CMAKE_BINARY_DIR}/common
                          "<SOURCE_DIR>"
        BUILD_COMMAND     "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release
        # Install our 4 named components only — WhiteoutFlakes's subdir adds
        # default install rules for its headers/libs (Unspecified component);
        # those are dev artifacts we don't ship.
        INSTALL_COMMAND   "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release
                                              --prefix "${WDX_STAGE_DIR}" --component plugins
                  COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release
                                              --prefix "${WDX_STAGE_DIR}" --component standalone
                  COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release
                                              --prefix "${WDX_STAGE_DIR}" --component scripts
                  COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release
                                              --prefix "${WDX_STAGE_DIR}" --component manifest
        DEPENDS           common_libs
        BUILD_ALWAYS      TRUE
    )
    list(APPEND _per_version_targets ${_tgt})
endforeach()

# ============================================================================
# Managed wrappers — C++/CLI WhiteoutDexTextureBrowser.dll
#
# Driven by MSBuild on the existing .vcxproj files because C++/CLI doesn't fit
# CMake's native generator. The native .lib comes from the common_libs sub-build;
# fw48 covers Max 2016-2025, net8 covers Max 2026-2027.
# ============================================================================
set(WDX_BUILD_MANAGED_WRAPPERS ON CACHE BOOL
    "Build the C++/CLI WhiteoutDexTextureBrowser.dll managed wrappers")

if(WDX_BUILD_MANAGED_WRAPPERS)
    # Resolve MSBuild from VSWhere (matches Build.bat).
    find_program(VSWHERE_EXE vswhere
        PATHS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer"
        NO_DEFAULT_PATH
    )

    set(MSBUILD_EXE "" CACHE FILEPATH "Path to MSBuild.exe (auto-detected from vswhere)")
    set(_platform_toolset "v143")
    if(VSWHERE_EXE AND NOT MSBUILD_EXE)
        execute_process(
            COMMAND "${VSWHERE_EXE}" -latest -products * -requires Microsoft.Component.MSBuild
                    -find "MSBuild/**/Bin/MSBuild.exe"
            OUTPUT_VARIABLE _msbuild_out
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_msbuild_out)
            set(MSBUILD_EXE "${_msbuild_out}" CACHE FILEPATH "" FORCE)
        endif()
        # VS 2026 raises the toolset to v145; detect by querying for it.
        execute_process(
            COMMAND "${VSWHERE_EXE}" -version "[18.0,19.0)" -property installationPath
            OUTPUT_VARIABLE _vs2026_install
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_vs2026_install)
            set(_platform_toolset "v145")
        endif()
    endif()

    if(NOT MSBUILD_EXE)
        message(WARNING "MSBuild not found — managed wrappers will be skipped. "
                        "Set -DMSBUILD_EXE=<path> to enable.")
    else()
        message(STATUS "Managed wrappers: MSBuild = ${MSBUILD_EXE} (toolset=${_platform_toolset})")

        set(_wrapper_dir "${CMAKE_SOURCE_DIR}/src/native_sources/WhiteoutDexTextureBrowser/wrapper")
        set(_wrapper_out "${CMAKE_SOURCE_DIR}/src/native_sources/WhiteoutDexTextureBrowser/output")
        set(_common_native "${CMAKE_BINARY_DIR}/common")

        # Does this build target any Max ≤ 2025? (fw48 covers those.)
        # Does this build target any Max ≥ 2026? (net8 covers those.)
        set(_need_fw48 FALSE)
        set(_need_net8 FALSE)
        foreach(v IN LISTS WDX_MAX_VERSIONS)
            if(v LESS 2026)
                set(_need_fw48 TRUE)
            else()
                set(_need_net8 TRUE)
            endif()
        endforeach()

        set(_managed_targets "")

        # ---- .NET Framework 4.8 (Max 2016-2025) ----
        if(_need_fw48)
            ExternalProject_Add(managed_fw48
                SOURCE_DIR        "${_wrapper_dir}"
                BUILD_IN_SOURCE   TRUE
                CONFIGURE_COMMAND ""
                BUILD_COMMAND     "${MSBUILD_EXE}" WhiteoutDexTextureBrowser_fw48.vcxproj
                                  /p:Configuration=Release /p:Platform=x64
                                  /p:PlatformToolset=${_platform_toolset}
                                  /p:NativeBuildDir=${_common_native}
                                  /v:minimal
                INSTALL_COMMAND   ""
                DEPENDS           common_libs
                BUILD_ALWAYS      TRUE
            )
            list(APPEND _managed_targets managed_fw48)
        endif()

        # ---- .NET 8.0 (Max 2026-2027) ----
        # Probe for the .NET 8 SDK; the .vcxproj references Microsoft.NET.Sdk which
        # MSBuild can only resolve when MSBuildSDKsPath points at a .NET 8 SDK's
        # Sdks/ dir. Skip the target entirely when nothing's installed.
        set(_dotnet8_sdks "")
        file(GLOB _dotnet8_candidates "$ENV{ProgramFiles}/dotnet/sdk/8.*")
        foreach(_cand IN LISTS _dotnet8_candidates)
            if(IS_DIRECTORY "${_cand}/Sdks")
                set(_dotnet8_sdks "${_cand}/Sdks")
            endif()
        endforeach()

        if(_need_net8 AND NOT _dotnet8_sdks)
            message(WARNING ".NET 8 SDK not found under "
                            "$ENV{ProgramFiles}/dotnet/sdk/. Max ≥ 2026 will be "
                            "deployed without WhiteoutDexTextureBrowser.dll. "
                            "Install the .NET 8 SDK to enable.")
            set(_need_net8 FALSE)
        endif()

        if(_need_net8)
            message(STATUS "Managed wrappers: .NET 8 SDKs = ${_dotnet8_sdks}")
            ExternalProject_Add(managed_net8
                SOURCE_DIR        "${_wrapper_dir}"
                BUILD_IN_SOURCE   TRUE
                CONFIGURE_COMMAND ""
                BUILD_COMMAND     "${CMAKE_COMMAND}" -E env "MSBuildSDKsPath=${_dotnet8_sdks}"
                                  "${MSBUILD_EXE}" WhiteoutDexTextureBrowser_net8.vcxproj
                                  /p:Configuration=Release /p:Platform=x64
                                  /p:PlatformToolset=${_platform_toolset}
                                  /p:NativeBuildDir=${_common_native}
                                  /v:minimal
                INSTALL_COMMAND   ""
                DEPENDS           common_libs
                BUILD_ALWAYS      TRUE
            )
            list(APPEND _managed_targets managed_net8)
        endif()

        # ---- Stage the managed DLLs alongside the per-version native plugins.
        # fw48 → Max 2016-2025, net8 → Max 2026-2027.
        set(_managed_fw48_dll "${_wrapper_out}/Max2016-2025/WhiteoutDexTextureBrowser.dll")
        set(_managed_net8_dll "${_wrapper_out}/Max2026-2027/WhiteoutDexTextureBrowser.dll")

        set(_stage_cmds "")
        foreach(v IN LISTS WDX_MAX_VERSIONS)
            if(v LESS 2026 AND _need_fw48)
                list(APPEND _stage_cmds COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                     "${_managed_fw48_dll}" "${WDX_STAGE_DIR}/Max${v}/WhiteoutDexTextureBrowser.dll")
            elseif(NOT (v LESS 2026) AND _need_net8)
                list(APPEND _stage_cmds COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                     "${_managed_net8_dll}" "${WDX_STAGE_DIR}/Max${v}/WhiteoutDexTextureBrowser.dll")
            endif()
        endforeach()

        if(_managed_targets AND _stage_cmds)
            add_custom_target(managed_stage ALL
                ${_stage_cmds}
                COMMENT "Staging WhiteoutDexTextureBrowser.dll into per-version dirs"
                VERBATIM
            )
            add_dependencies(managed_stage ${_managed_targets} ${_per_version_targets})
        endif()
    endif()
endif()

# ============================================================================
# Deployment targets
#
# By the time these run, ${WDX_STAGE_DIR} is the canonical output: every Max
# version has its own plugins/ subdir, Standalone/ is unified, scripts/ and
# PackageContents.xml are at the root. The deploy targets just copy that tree
# to different prefixes — Build.bat's `output/` and `%APPDATA%/...` phases.
# ============================================================================

# ---- output/ layout (suite) ----
add_custom_target(deploy_output
    COMMAND "${CMAKE_COMMAND}" -E make_directory   "${CMAKE_BINARY_DIR}/output"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory   "${WDX_STAGE_DIR}" "${CMAKE_BINARY_DIR}/output"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_BINARY_DIR}/output/scripts"
    COMMAND "${CMAKE_COMMAND}" -E remove           "${CMAKE_BINARY_DIR}/output/PackageContents.xml"
    DEPENDS ${_per_version_targets}
    COMMENT "Deploying suite layout to ${CMAKE_BINARY_DIR}/output"
    VERBATIM
)

# ---- AppData layout (3ds Max ApplicationPlugins) ----
# %APPDATA%/Autodesk/ApplicationPlugins/WhiteoutDex/
#   PackageContents.xml
#   scripts/{macroscripts,pre_startup_scripts,post_startup_scripts,scripted_plugins}/
#   native plugins/Max<year>/*.{dle,dlo,dlx,bmi,dll}
set(_appdata_root "$ENV{APPDATA}/Autodesk/ApplicationPlugins/WhiteoutDex")

set(_appdata_cmds
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_appdata_root}/native plugins"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_appdata_root}/scripts"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${WDX_STAGE_DIR}/scripts" "${_appdata_root}/scripts"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${WDX_STAGE_DIR}/PackageContents.xml" "${_appdata_root}/PackageContents.xml"
)
foreach(v IN LISTS WDX_MAX_VERSIONS)
    list(APPEND _appdata_cmds
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${WDX_STAGE_DIR}/Max${v}" "${_appdata_root}/native plugins/Max${v}"
    )
endforeach()

add_custom_target(deploy_appdata
    ${_appdata_cmds}
    DEPENDS ${_per_version_targets}
    COMMENT "Deploying to ${_appdata_root}"
    VERBATIM
)
if(TARGET managed_stage)
    add_dependencies(deploy_output  managed_stage)
    add_dependencies(deploy_appdata managed_stage)
endif()
