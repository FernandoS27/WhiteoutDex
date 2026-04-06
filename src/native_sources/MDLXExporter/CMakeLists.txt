# ============================================================================
# MDLXExporter — MDX/MDL Scene Exporter (.dle)
# Links: MaxCore (static, scene access + animation)
#        whiteout_lib    (MDX writer)
#        maxscrpt.lib    (Note track / CA access)
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(MDLXExporter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h")

add_library(MDLXExporter SHARED ${SOURCES})

target_include_directories(MDLXExporter PRIVATE src)

target_link_libraries(MDLXExporter PRIVATE
    MaxCore
    whiteout_lib
    maxscrpt.lib
)

target_compile_options(MDLXExporter PRIVATE
    /W3 /EHsc /Zp8 /bigobj /utf-8
    /wd4100 /wd4127 /wd4244 /wd4267
    /permissive
    /Zc:strictStrings-
)

target_sources(MDLXExporter PRIVATE src/MDLXExporter.def src/MDLXExporter.rc)

set_target_properties(MDLXExporter PROPERTIES
    SUFFIX ".dle"
    OUTPUT_NAME "MDLXExporter"
)
