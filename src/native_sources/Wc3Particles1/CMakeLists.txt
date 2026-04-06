# ============================================================================
# Wc3Particles1  -  Spherical Particle Emitter (.dlo)
# Parent provides: MaxSDK (interface lib)
# ============================================================================
add_library(Wc3Particles1 SHARED
    Particles.h
    Particles.cpp
    resource.h
    Wc3Particles1.rc
    Wc3Particles1.def
)

set_target_properties(Wc3Particles1 PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    SUFFIX                ".dlo"
    OUTPUT_NAME           "Wc3Particles1"
)

target_include_directories(Wc3Particles1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(Wc3Particles1 PRIVATE /W3 /Zi /EHsc /utf-8)

target_link_libraries(Wc3Particles1 PRIVATE
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