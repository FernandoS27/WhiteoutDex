# ============================================================================
# Wc3Ribbon  -  Ribbon Trail Emitter (.dlo)
# Parent provides: MaxSDK (interface lib)
# ============================================================================
add_library(Wc3Ribbon SHARED
    Ribbon.cpp
    Ribbon.h
    resource.h
    Wc3Ribbon.rc
    Wc3Ribbon.def
)

set_target_properties(Wc3Ribbon PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    SUFFIX                ".dlo"
    OUTPUT_NAME           "Wc3Ribbon"
)

target_include_directories(Wc3Ribbon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(Wc3Ribbon PRIVATE /W3 /Zi /EHsc /utf-8)

target_link_libraries(Wc3Ribbon PRIVATE
    MaxSDK
    core.lib
    geom.lib
    gfx.lib
    mesh.lib
    maxutil.lib
    maxscrpt.lib
    paramblk2.lib
    comctl32.lib
)