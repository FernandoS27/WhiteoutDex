# ============================================================================
# blp-max  -  BLP Bitmap I/O plugin (.bmi)
# Parent provides: MaxSDK (interface lib), whiteout_lib
# ============================================================================
add_library(blp SHARED
    blpio.h
    blpio.cpp
    blpdll.cpp
    blp.def
)

set_target_properties(blp PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    SUFFIX                ".bmi"
    PREFIX                ""
    OUTPUT_NAME           "blp"
)

target_include_directories(blp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(blp PRIVATE /W3 /Zi /FS /EHsc /Zp8 /bigobj /utf-8
    /permissive
    /Zc:strictStrings-
)

target_link_libraries(blp PRIVATE
    MaxSDK
    whiteout_lib
    core.lib
    geom.lib
    maxutil.lib
    bmm.lib
    comctl32.lib
)