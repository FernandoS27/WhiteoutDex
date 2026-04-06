cmake_minimum_required(VERSION 3.20)
project(MaxCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE CORE_SOURCES "src/*.cpp" "src/*.h")

add_library(MaxCore STATIC ${CORE_SOURCES})

target_include_directories(MaxCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Max SDK comes from parent — use the INTERFACE library
target_link_libraries(MaxCore PUBLIC MaxSDK)

target_link_libraries(MaxCore PUBLIC
    core.lib
    geom.lib
    mesh.lib
    maxutil.lib
    bmm.lib
    paramblk2.lib
    MNMath.lib
)

target_compile_options(MaxCore PRIVATE
    /W3 /EHsc /Zp8 /bigobj /utf-8
    /wd4100 /wd4127 /wd4244 /wd4267
    /permissive
    /Zc:strictStrings-
)
