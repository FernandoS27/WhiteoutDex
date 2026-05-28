# ============================================================================
# MDLXImporter — MDX/MDL Scene Importer (.dle)
# Links: MaxCore                          (static, scene construction + IR types)
#        whiteout_lib                     (MDX parser)
#        maxscrpt.lib                     (Scripted plugin / paramblock access)
#        WhiteoutDexTextureBrowser_native (MPQ archive reader, sibling project)
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(MDLXImporter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h")

add_library(MDLXImporter SHARED ${SOURCES} src/MDLXImporter.rc)

target_include_directories(MDLXImporter PRIVATE src)

target_link_libraries(MDLXImporter PRIVATE
    MaxCore
    whiteout_lib
    maxscrpt.lib
)

if(TARGET whiteout_casc)
    target_link_libraries(MDLXImporter PRIVATE whiteout_casc)
endif()

# ── MPQ archive support ───────────────────────────────────────────────────
# Required by src/scene/mpq_texture_resolver.cpp. Pulls in whiteout_mpq
# transitively through WhiteoutDexTextureBrowser_native.
if(TARGET WhiteoutDexTextureBrowser_native)
    target_link_libraries(MDLXImporter PRIVATE WhiteoutDexTextureBrowser_native)
    message(STATUS "MDLXImporter: MPQ via WhiteoutDexTextureBrowser_native target")
elseif(TARGET whiteout_mpq)
    target_link_libraries(MDLXImporter PRIVATE whiteout_mpq)
    message(STATUS "MDLXImporter: MPQ via whiteout_mpq target")
else()
    message(FATAL_ERROR
        "MDLXImporter: neither WhiteoutDexTextureBrowser_native nor whiteout_mpq "
        "is available. MPQ texture extraction requires one of them in the parent "
        "CMake hierarchy.")
endif()

target_compile_options(MDLXImporter PRIVATE
    /W3 /EHsc /Zp8 /bigobj /utf-8
    /wd4100 /wd4127 /wd4244 /wd4267
    /permissive
    /Zc:strictStrings-
)

target_sources(MDLXImporter PRIVATE src/MDLXImporter.def)

set_target_properties(MDLXImporter PROPERTIES
    SUFFIX ".dle"
    OUTPUT_NAME "MDLXImporter"
)
