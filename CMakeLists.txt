cmake_minimum_required(VERSION 3.20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(WdxMaxSdk)

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
# Detect installed Max SDKs. Caller can override with
# -DWDX_MAX_VERSIONS="2025;2026" to limit the set, and each year's resolved
# maxsdk is forwarded to its sub-build so the search happens exactly once.
# ----------------------------------------------------------------------------
set(WDX_MAX_YEARS 2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027
    CACHE STRING "Max release years this build knows how to target")

set(_wdx_detected "")
set(_wdx_sdkless "")
foreach(v IN LISTS WDX_MAX_YEARS)
    wdx_find_max_sdk(${v} _sdk)
    if(_sdk)
        list(APPEND _wdx_detected "${v}")
        set(WDX_MAX_SDK_${v} "${_sdk}")
    else()
        # The product without its SDK: worth calling out by name, because it
        # looks identical to "unsupported year" from the detected list alone.
        wdx_max_install_dir(${v} _installed)
        if(_installed)
            list(APPEND _wdx_sdkless "${v}")
        endif()
    endif()
endforeach()

set(WDX_MAX_VERSIONS "${_wdx_detected}" CACHE STRING
    "Semicolon-separated Max versions to build (default: every installed SDK)")

if(_wdx_sdkless)
    message(STATUS
        "WhiteoutDex: 3ds Max ${_wdx_sdkless} installed, but no matching SDK — "
        "skipping. Install the SDK (Autodesk ships it separately from the "
        "product), or point at an unpacked copy with "
        "-DMAXSDK_PATH_<year>=<path-to-maxsdk>.")
endif()

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
    # common_libs stays on /MD because its output feeds the C++/CLI managed
    # wrapper, which is required to use the shared CRT. The per-version Max
    # plug-in sub-builds below get their own /MT copy of whiteout_lib (we no
    # longer pass PREBUILT_COMMON_DIR to them) so the .dlx / .dle / .dlo /
    # .bmi binaries have no external VCRUNTIME140* dependency.
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
# Link the MSVC runtime dynamically (/MD) — the *known-good* WhiteoutDex
# binary that loads on Max 2016 was built this way and pulls in MSVCP140 /
# VCRUNTIME140 / VCRUNTIME140_1 / api-ms-win-crt-*. Switching to /MT
# (static runtime) caused Max 2016 to silently reject every .dlx — even a
# 118 KB probe with no transitive dependencies — while a /MD build of the
# same code loads cleanly. The originally claimed motivation for /MT
# ("clean Max 2016 hosts lack the VC++ Redist") was a misdiagnosis of an
# unrelated PackageContents.xml schema bug that we have since fixed.
# Distribute the VC++ 2015-2022 Redistributable alongside the installer
# if shipping to machines that haven't picked it up via Windows Update.
set(_wdx_static_runtime_args
    -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL$<$<CONFIG:Debug>:Debug>"
    # Disable Tracy + WebGPU in the per-version sub-builds: both run
    # aggressive C++ global-init machinery (Tracy spins worker threads and
    # opens a TCP listener; Dawn registers backends from static
    # constructors). On Max 2016 hosts those have been observed to make
    # the plug-in silently fail to load — Max's plug-in loader gives no
    # error, ndxStart just never registers. The renderer's API treats
    # these as opt-in instrumentation, so turning them off doesn't affect
    # the preview window functionality the plug-in actually needs.
    -DWDX_ENABLE_TRACY=OFF
    -DWDX_ENABLE_WEBGPU=OFF
)

set(_per_version_targets "")
foreach(v IN LISTS WDX_MAX_VERSIONS)
    set(_tgt plugins_Max${v})
    ExternalProject_Add(${_tgt}
        SOURCE_DIR        "${CMAKE_SOURCE_DIR}/src/native_sources"
        BINARY_DIR        "${CMAKE_BINARY_DIR}/build_${v}"
        # No PREBUILT_COMMON_DIR — that would pull /MD common_libs into a
        # /MT plug-in build (link error LNK2038). Each per-version sub-build
        # rebuilds whiteout_lib + zlib statically; the cost is the only-once
        # whiteout_lib compile per Max year.
        CONFIGURE_COMMAND "${CMAKE_COMMAND}" ${_sub_gen_args}
                          -DMAX_VERSION=${v}
                          # Hand down the SDK we already located, so a sub-build
                          # can't disagree with the year list about where (or
                          # whether) that year's SDK is.
                          "-DMAXSDK_PATH=${WDX_MAX_SDK_${v}}"
                          ${_wdx_static_runtime_args}
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
# fw48 covers Max 2016-2025, net8 covers Max 2026, net10 covers Max 2027+.
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

        # Which lane each Max year needs. A C++/CLI assembly only loads into
        # the runtime it was compiled against, so this is dictated by the CLR
        # each Max hosts — read off its own 3dsmax_interop.runtimeconfig.json —
        # not by which SDK we happen to have:
        #   2016-2025 → .NET Framework 4.8
        #   2026      → .NET 8   (net8.0)
        #   2027+     → .NET 10  (net10.0)
        # Adding a year means adding its lane here; there is no "newest wins",
        # because a net10.0 assembly will not load in Max 2026's .NET 8 host
        # any more than a net8.0 one is what Max 2027 asks for.
        set(_need_fw48 FALSE)
        set(_need_net8 FALSE)
        set(_need_net10 FALSE)
        foreach(v IN LISTS WDX_MAX_VERSIONS)
            if(v LESS 2026)
                set(_need_fw48 TRUE)
            elseif(v LESS 2027)
                set(_need_net8 TRUE)
            else()
                set(_need_net10 TRUE)
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

        # ---- .NET lanes (Max 2026 = net8.0, Max 2027+ = net10.0) ----
        # These .vcxproj files are SDK-style: <CLRSupport>NetCore</CLRSupport>
        # makes Microsoft.CppCommon.targets import Microsoft.NET.Sdk, and
        # MSBuild.exe can only resolve that through the .NET SDK resolver that
        # Visual Studio's ".NET SDK" individual component installs at
        #   <VS>/MSBuild/Current/Bin/SdkResolvers/Microsoft.DotNet.MSBuildSdkResolver/
        #
        # There is no substitute for it. The SDK ships its own resolver
        # (dotnet/sdk/<ver>/Microsoft.DotNet.SdkResolver.dll) but that one is a
        # .NET assembly — handing it to MSBuild.exe through
        # MSBUILDADDITIONALSDKRESOLVERSFOLDER gets "MSB4244 … an attempt was
        # made to load a program with an incorrect format". Only VS ships the
        # .NET Framework build.
        #
        # Setting MSBuildSDKsPath by hand — which is what this used to do —
        # looks like it works, because evaluation finds the targets. It then
        # dies once the SDK's own tasks run: ProcessFrameworkReferences throws
        # FileNotFoundException on Microsoft.Deployment.DotNet.Releases, and
        # the workload locator SDKs don't resolve either. Verified identical
        # for net8.0 and net10.0, under VS 2022's MSBuild and VS 18's, with
        # SDK 10.0.300 and 10.0.400, and for a bare C# net10.0 classlib — so
        # it is not this project, the TFM, the SDK band or the toolset. With
        # the resolver present the override is not merely unnecessary but
        # harmful, since it pre-empts the resolver; so it is gone.
        #
        # Search DOTNET_ROOT and whatever `dotnet` is on PATH as well as
        # %ProgramFiles%\dotnet — a machine can perfectly well keep it
        # elsewhere.
        get_filename_component(_msbuild_bin "${MSBUILD_EXE}" DIRECTORY)
        set(_dotnet_sdk_resolver
            "${_msbuild_bin}/SdkResolvers/Microsoft.DotNet.MSBuildSdkResolver")

        set(_dotnet_roots "")
        if(DEFINED ENV{DOTNET_ROOT})
            file(TO_CMAKE_PATH "$ENV{DOTNET_ROOT}" _dotnet_root)
            list(APPEND _dotnet_roots "${_dotnet_root}")
        endif()
        if(DEFINED ENV{ProgramFiles})
            file(TO_CMAKE_PATH "$ENV{ProgramFiles}" _pf)
            list(APPEND _dotnet_roots "${_pf}/dotnet")
        endif()
        find_program(DOTNET_EXE dotnet DOC "dotnet CLI (used to locate a .NET SDK)")
        if(DOTNET_EXE)
            get_filename_component(_dotnet_root "${DOTNET_EXE}" DIRECTORY)
            list(APPEND _dotnet_roots "${_dotnet_root}")
        endif()
        list(REMOVE_DUPLICATES _dotnet_roots)

        # Newest installed SDK of exactly band @p band. Band-exact, not "that
        # band or newer": a .vcxproj can't restore from NuGet, so the targeting
        # and apphost packs have to be on disk already, and an SDK only ships
        # its own band's. Building the net8.0 wrapper with the 10.0.400 SDK
        # gets as far as "NETSDK1145: The Apphost pack is not installed and
        # NuGet package restore is not supported … Pack Package Version:
        # 8.0.30".
        function(_wdx_find_dotnet_sdk band out_var)
            set(_best "")
            foreach(_root IN LISTS _dotnet_roots)
                file(GLOB _cands "${_root}/sdk/${band}.*")
                foreach(_cand IN LISTS _cands)
                    get_filename_component(_ver "${_cand}" NAME)
                    if(IS_DIRECTORY "${_cand}/Sdks" AND "${_ver}" VERSION_GREATER "${_best}")
                        set(_best "${_ver}")
                    endif()
                endforeach()
            endforeach()
            set(${out_var} "${_best}" PARENT_SCOPE)
        endfunction()

        # A macro, not a function: the "can't build this lane" paths clear
        # _need_<lane> so the staging loop below skips that lane's copy, and
        # that has to land in this scope.
        macro(_wdx_add_dotnet_lane lane band proj)
            if(_need_${lane})
                _wdx_find_dotnet_sdk(${band} _lane_sdk)
                if(NOT EXISTS "${_dotnet_sdk_resolver}")
                    message(WARNING
                        "Visual Studio is missing its \".NET SDK\" component, so "
                        "MSBuild has no .NET SDK resolver at "
                        "${_dotnet_sdk_resolver} and cannot build the "
                        "net${band}.0 managed wrapper. Max versions on that "
                        "runtime will be deployed without "
                        "WhiteoutDexTextureBrowser.dll — everything else still "
                        "builds. To enable: Visual Studio Installer → Modify → "
                        "Individual components → \".NET SDK\".")
                    set(_need_${lane} FALSE)
                elseif(NOT _lane_sdk)
                    message(WARNING
                        "No .NET ${band} SDK found under ${_dotnet_roots} "
                        "(sdk/${band}.*). Max versions on the net${band}.0 "
                        "runtime will be deployed without "
                        "WhiteoutDexTextureBrowser.dll — everything else still "
                        "builds. Install the .NET ${band} SDK to enable; a "
                        "newer SDK does not substitute, because it ships only "
                        "its own band's targeting and apphost packs and a "
                        ".vcxproj cannot restore them from NuGet.")
                    set(_need_${lane} FALSE)
                else()
                    message(STATUS "Managed wrappers: net${band}.0 via .NET SDK ${_lane_sdk}")
                    ExternalProject_Add(managed_${lane}
                        SOURCE_DIR        "${_wrapper_dir}"
                        BUILD_IN_SOURCE   TRUE
                        CONFIGURE_COMMAND ""
                        BUILD_COMMAND     "${MSBUILD_EXE}" ${proj}
                                          /p:Configuration=Release /p:Platform=x64
                                          /p:PlatformToolset=${_platform_toolset}
                                          /p:NativeBuildDir=${_common_native}
                                          /v:minimal
                        INSTALL_COMMAND   ""
                        DEPENDS           common_libs
                        BUILD_ALWAYS      TRUE
                    )
                    list(APPEND _managed_targets managed_${lane})
                endif()
            endif()
        endmacro()

        _wdx_add_dotnet_lane(net8   8 WhiteoutDexTextureBrowser_net8.vcxproj)
        _wdx_add_dotnet_lane(net10 10 WhiteoutDexTextureBrowser_net10.vcxproj)

        # ---- Stage the managed DLLs alongside the per-version native plugins.
        # Each lane writes its own output/Max* dir; the copy is per Max year
        # because that's the layout WhiteoutDexTextureBrowserHelper.ms probes
        # first.
        set(_managed_fw48_dll  "${_wrapper_out}/Max2016-2025/WhiteoutDexTextureBrowser.dll")
        set(_managed_net8_dll  "${_wrapper_out}/Max2026/WhiteoutDexTextureBrowser.dll")
        set(_managed_net10_dll "${_wrapper_out}/Max2027/WhiteoutDexTextureBrowser.dll")

        set(_stage_cmds "")
        foreach(v IN LISTS WDX_MAX_VERSIONS)
            if(v LESS 2026)
                set(_lane fw48)
            elseif(v LESS 2027)
                set(_lane net8)
            else()
                set(_lane net10)
            endif()
            if(_need_${_lane})
                list(APPEND _stage_cmds COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                     "${_managed_${_lane}_dll}"
                     "${WDX_STAGE_DIR}/Max${v}/WhiteoutDexTextureBrowser.dll")
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

# ============================================================================
# Inno Setup installer (`installer` target)
#
# Configures packaging/Whiteout_Setup.iss.in into a build-local .iss with all
# Source/ paths pointing at the stage tree, then runs iscc.exe to produce a
# single .exe under <build>/installer/. The per-version [Files] block is
# generated from WDX_MAX_VERSIONS, so the installer ships exactly the Max
# versions this build actually produced.
#
# Override the Inno Setup compiler with -DISCC=<path>; auto-detected from
# the standard "C:\Program Files (x86)\Inno Setup 6" install otherwise.
# ============================================================================
find_program(ISCC ISCC.exe
    PATHS
        "$ENV{ProgramFiles\(x86\)}/Inno Setup 6"
        "$ENV{ProgramFiles}/Inno Setup 6"
        "$ENV{ProgramFiles\(x86\)}/Inno Setup 5"
    DOC "Inno Setup compiler (iscc.exe)")

if(NOT ISCC)
    message(STATUS "Inno Setup compiler not found — `installer` target will be unavailable. "
                   "Install Inno Setup 6 from https://jrsoftware.org/isdl.php, "
                   "or pass -DISCC=<path-to-iscc.exe>.")
else()
    message(STATUS "Inno Setup: ${ISCC}")

    # Read version from version.txt at the repo root so the installer stays
    # in lockstep with whatever the runtime reports.
    if(EXISTS "${CMAKE_SOURCE_DIR}/version.txt")
        file(READ "${CMAKE_SOURCE_DIR}/version.txt" WDX_VERSION)
        string(STRIP "${WDX_VERSION}" WDX_VERSION)
    else()
        set(WDX_VERSION "0.0.0")
    endif()

    set(WDX_REPO_ROOT             "${CMAKE_SOURCE_DIR}")
    set(WDX_INSTALLER_OUTPUT_DIR  "${CMAKE_BINARY_DIR}/installer")

    # Emit one [Files] line per (Max version × plugin file). skipifsourcedoesntexist
    # lets a partial stage tree (e.g. .NET 8 SDK missing → no managed wrapper for
    # Max ≥ 2026) still pack into a valid installer.
    set(WDX_NATIVE_PLUGINS_BLOCK "")
    foreach(v IN LISTS WDX_MAX_VERSIONS)
        string(APPEND WDX_NATIVE_PLUGINS_BLOCK
            "Source: \"${WDX_STAGE_DIR}\\Max${v}\\*\"; "
            "DestDir: \"{app}\\native plugins\\Max${v}\"; "
            "Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist\n")
    endforeach()

    # ----------------------------------------------------------------------
    # PackageContents.xml — generated alongside the .iss so the manifest
    # tracks WDX_MAX_VERSIONS, the actual installer layout (native plugins/,
    # scripts/), and the live MaxScript file set.
    # ----------------------------------------------------------------------

    # The shipped per-version plugin set — only files Max loads via the native
    # plug-in ABI (LibVersion / LibNumberClasses / …). WhiteoutDexTextureBrowser.dll
    # is the C++/CLI managed wrapper; it's loaded via dotNet.loadAssembly from
    # WhiteoutDexTextureBrowserHelper.ms at runtime, so listing it here makes
    # Max try (and fail) to bind to native exports it doesn't have. The .dll
    # still ships into the same Max<year>/ folder because the installer copies
    # the entire stage tree — we just don't reference it from the manifest.
    set(_wdx_native_plugin_files
        blp.bmi
        MDLXExporter.dle
        MDLXImporter.dle
        Wc3Particles1.dlo
        Wc3Particles2.dlo
        Wc3Ribbon.dlo
        WhiteoutDexRenderer.dlx
    )

    list(SORT WDX_MAX_VERSIONS COMPARE NATURAL)
    list(GET WDX_MAX_VERSIONS 0 WDX_SERIES_MIN)
    list(GET WDX_MAX_VERSIONS -1 WDX_SERIES_MAX)

    # One <Components> block per Max version, listing every plug-in for
    # that year inside it. This matches the original working PackageContents
    # schema; emitting one <Components> block per (version × plugin) caused
    # Max 2016 to silently drop every entry past the first identical-spec
    # block, leaving every .dlx / .bmi / .dle / .dlo unloaded.
    set(WDX_NATIVE_PLUGINS_COMPONENTS "")
    foreach(v IN LISTS WDX_MAX_VERSIONS)
        string(APPEND WDX_NATIVE_PLUGINS_COMPONENTS
            "    <!-- Native Plugins Max ${v} -->\n"
            "    <Components Description=\"plugins parts\">\n"
            "        <RuntimeRequirements OS=\"Win64\" Platform=\"3ds Max\""
            " SeriesMin=\"${v}\" SeriesMax=\"${v}\" />\n")
        foreach(_p IN LISTS _wdx_native_plugin_files)
            string(APPEND WDX_NATIVE_PLUGINS_COMPONENTS
                "        <ComponentEntry ModuleName=\"./native plugins/Max${v}/${_p}\" />\n")
        endforeach()
        string(APPEND WDX_NATIVE_PLUGINS_COMPONENTS
            "    </Components>\n\n")
    endforeach()

    # MaxScript ComponentEntry lists, derived from CONFIGURE_DEPENDS globs so
    # CMake re-runs whenever a script is added/removed. The MenuCallback /
    # MenuInstaller files are emitted in their own version-restricted blocks
    # by the template, so we exclude them here.
    function(_wdx_emit_script_entries out_var subdir glob excludes)
        file(GLOB _files CONFIGURE_DEPENDS
             "${CMAKE_SOURCE_DIR}/src/${subdir}/${glob}")
        list(SORT _files)
        set(_lines "")
        foreach(_f IN LISTS _files)
            get_filename_component(_name "${_f}" NAME)
            if(_name IN_LIST excludes)
                continue()
            endif()
            string(APPEND _lines
                "        <ComponentEntry ModuleName=\"./scripts/${subdir}/${_name}\" />\n")
        endforeach()
        set(${out_var} "${_lines}" PARENT_SCOPE)
    endfunction()

    _wdx_emit_script_entries(WDX_PRE_STARTUP_ENTRIES
        "pre_startup_scripts" "*.ms" "WhiteoutDexMenuCallback.ms")
    _wdx_emit_script_entries(WDX_POST_STARTUP_ENTRIES
        "post_startup_scripts" "*.ms" "WhiteoutDexMenuInstaller.ms")
    _wdx_emit_script_entries(WDX_SCRIPTED_PLUGINS_ENTRIES
        "scripted_plugins" "*.ms" "")
    _wdx_emit_script_entries(WDX_MACROSCRIPTS_ENTRIES
        "macroscripts" "*.mcr" "")

    # Per-version menu glue. The menu API changed in Max 2025: 2016-2024 load
    # WhiteoutDexMenuInstaller.ms (post-startup), 2025+ load WhiteoutDexMenuCallback.ms
    # (pre-startup). Emit each <Components> block only when WDX_MAX_VERSIONS
    # actually overlaps the relevant range — otherwise the inverted
    # SeriesMin/SeriesMax fails Max's manifest validation.
    set(WDX_MENU_CALLBACK_COMPONENTS "")
    if(WDX_SERIES_MAX GREATER_EQUAL 2025)
        if(WDX_SERIES_MIN GREATER 2025)
            set(_callback_min ${WDX_SERIES_MIN})
        else()
            set(_callback_min 2025)
        endif()
        set(WDX_MENU_CALLBACK_COMPONENTS
"
    <!-- Max 2025+: new menu callback API. -->
    <Components Description=\"pre-start-up scripts parts\">
        <RuntimeRequirements OS=\"Win64\" Platform=\"3ds Max\"
                             SeriesMin=\"${_callback_min}\" SeriesMax=\"${WDX_SERIES_MAX}\" />
        <ComponentEntry ModuleName=\"./scripts/pre_startup_scripts/WhiteoutDexMenuCallback.ms\" />
    </Components>")
    endif()

    set(WDX_MENU_INSTALLER_COMPONENTS "")
    if(WDX_SERIES_MIN LESS_EQUAL 2024)
        if(WDX_SERIES_MAX LESS 2024)
            set(_installer_max ${WDX_SERIES_MAX})
        else()
            set(_installer_max 2024)
        endif()
        set(WDX_MENU_INSTALLER_COMPONENTS
"
    <!-- Max 2016-2024: legacy menu installer. -->
    <Components Description=\"post-start-up scripts parts\">
        <RuntimeRequirements OS=\"Win64\" Platform=\"3ds Max\"
                             SeriesMin=\"${WDX_SERIES_MIN}\" SeriesMax=\"${_installer_max}\" />
        <ComponentEntry ModuleName=\"./scripts/post_startup_scripts/WhiteoutDexMenuInstaller.ms\" />
    </Components>")
    endif()

    # ProductCode must be a real {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} GUID
    # per the package spec — Max 2016 rejects the manifest with anything else.
    # Derive deterministically from the version so it stays stable for a given
    # release; the .iss's Pascal post-install hook overwrites it again with a
    # date-stamped variant.
    string(UUID WDX_PRODUCT_CODE_BARE
        NAMESPACE "4583feeb-c403-46e3-9c96-4563a4a34e24"
        NAME      "WhiteoutDex-${WDX_VERSION}"
        TYPE      SHA1
        UPPER)
    set(WDX_PRODUCT_CODE "{${WDX_PRODUCT_CODE_BARE}}")

    set(_pkg_in  "${CMAKE_SOURCE_DIR}/packaging/PackageContents.xml.in")
    set(_pkg_out "${CMAKE_BINARY_DIR}/packaging/PackageContents.xml")
    configure_file("${_pkg_in}" "${_pkg_out}" @ONLY)

    # Expose the generated PackageContents.xml path to the .iss template so
    # the installer ships the freshly-rendered manifest, not the stale one
    # at the repo root.
    set(WDX_PACKAGE_CONTENTS_XML "${_pkg_out}")

    # Generate the working .iss next to the template so relative paths inside
    # the script (compiler:Default.isl etc.) resolve identically.
    set(_iss_in  "${CMAKE_SOURCE_DIR}/packaging/Whiteout_Setup.iss.in")
    set(_iss_out "${CMAKE_BINARY_DIR}/packaging/Whiteout_Setup.iss")
    configure_file("${_iss_in}" "${_iss_out}" @ONLY)

    add_custom_target(installer
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${WDX_INSTALLER_OUTPUT_DIR}"
        COMMAND "${ISCC}" "${_iss_out}"
        DEPENDS ${_per_version_targets} "${_iss_in}" "${_pkg_in}"
        COMMENT "Building WhiteoutDex_Setup_v${WDX_VERSION}.exe via Inno Setup"
        VERBATIM
    )
    if(TARGET managed_stage)
        add_dependencies(installer managed_stage)
    endif()
endif()
