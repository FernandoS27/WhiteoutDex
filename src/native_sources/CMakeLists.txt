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
# Max SDK Configuration (skip when building only common libs)
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

# ===========================================================================
# Common output directories
#   Plugins (.bmi/.dlo/.dlx) -> <build>/plugins/<config>/
#   Static libs              -> default (keeps .vcxproj paths working)
# ===========================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")

# ===========================================================================
# MaxSDK - INTERFACE library with flags shared by all Max plugins
# ===========================================================================
add_library(MaxSDK INTERFACE)
target_include_directories(MaxSDK INTERFACE "${MAXSDK_PATH}/include")
target_link_directories(MaxSDK INTERFACE "${MAXSDK_PATH}/lib/x64/Release")
target_compile_definitions(MaxSDK INTERFACE
    WIN32
    _WIN64
    _WINDOWS
    _USRDLL
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

endif() # NOT BUILD_COMMON_ONLY

# ===========================================================================
# WhiteoutLib (BLP codec + CASC storage) & zlib
# ===========================================================================
set(WHITEOUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../external/WhiteoutLib"
    CACHE PATH "WhiteoutLib root")

if(NOT EXISTS "${WHITEOUT_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "WhiteoutLib not found at: ${WHITEOUT_DIR}")
endif()

if(PREBUILT_COMMON_DIR)
    # -----------------------------------------------------------------
    # Import pre-built common libraries (built once by BUILD_COMMON_ONLY)
    # -----------------------------------------------------------------
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
else()
    # -----------------------------------------------------------------
    # Build from source
    # -----------------------------------------------------------------
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
# Max SDK plugins (skip in common-only builds)
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
