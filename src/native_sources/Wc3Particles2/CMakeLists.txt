# ============================================================================
# Wc3Particles2  -  Advanced Particle Emitter (.dlo)
# Parent provides: MaxSDK (interface lib)
# ============================================================================
add_library(Wc3Particles2 SHARED
    Particles.h
    Particles.cpp
    resource.h
    Wc3Particles2.rc
    Wc3Particles2.def
)

set_target_properties(Wc3Particles2 PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    SUFFIX                ".dlo"
    OUTPUT_NAME           "Wc3Particles2"
)

target_include_directories(Wc3Particles2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(Wc3Particles2 PRIVATE /W3 /Zi /EHsc /utf-8)

target_link_libraries(Wc3Particles2 PRIVATE
    MaxSDK
    core.lib
    geom.lib
    gfx.lib
    mesh.lib
    particle.lib
    maxutil.lib
    paramblk2.lib
    maxscrpt.lib
    comctl32.lib
    mpr.lib
)