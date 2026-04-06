# ============================================================================
# WhiteoutDexNative  -  Archive & Texture Decoder (static library)
# Parent provides: whiteout_lib, whiteout_casc, zlibstatic
# No Max SDK dependency.
# ============================================================================
option(WHITEOUTDEX_BUILD_TESTS "Build standalone test executable" OFF)

add_library(WhiteoutDexNative_native STATIC
    Source/SharedPool.h
    Source/MPQReader.h
    Source/MPQReader.cpp
    Source/BLPDecoder.h
    Source/BLPDecoder.cpp
    Source/DDSDecoder.h
    Source/DDSDecoder.cpp
    Source/ThumbnailEngine.h
    Source/ThumbnailEngine.cpp
)

set_target_properties(WhiteoutDexNative_native PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    # Keep archive output at build root so .vcxproj wrapper finds it at
    # $(NativeBuildDir)/Release/WhiteoutDexNative_native.lib
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_include_directories(WhiteoutDexNative_native PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
)

target_link_libraries(WhiteoutDexNative_native PUBLIC
    whiteout_lib
    whiteout_casc
    zlibstatic
)

target_compile_definitions(WhiteoutDexNative_native PRIVATE
    _UNICODE
    UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WHITEOUT_HAS_CASC=1
)

if(MSVC)
    target_compile_options(WhiteoutDexNative_native PRIVATE
        /W4 /WX /EHsc /Zp8 /bigobj
        /wd4100 /wd4127 /wd4244 /wd4267
    )
endif()

# ---------------------------------------------------------------------------
# Standalone test (optional)
# ---------------------------------------------------------------------------
if(WHITEOUTDEX_BUILD_TESTS)
    add_executable(WhiteoutDexNative_test tests/MPQReaderTest.cpp)
    target_link_libraries(WhiteoutDexNative_test PRIVATE WhiteoutDexNative_native)
    if(MSVC)
        target_compile_options(WhiteoutDexNative_test PRIVATE /W3 /EHsc)
    endif()
endif()