# ============================================================================
# MDLXImporter — MDX/MDL Scene Importer (.dle)
# Links: MaxCore (static, scene construction + IR types)
#        whiteout_lib    (MDX parser)
#        maxscrpt.lib    (Scripted plugin creation / paramblock access)
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
