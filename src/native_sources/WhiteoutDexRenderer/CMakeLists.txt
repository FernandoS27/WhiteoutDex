# ============================================================================
# WhiteoutDexRenderer — Two targets:
#   1. WhiteoutDexRendererLib (STATIC) — standalone renderer, no Max SDK
#   2. WhiteoutDexExtractor   (SHARED .dlx) — Max plugin linking the renderer
# Parent provides: MaxSDK (interface lib), MAX_VERSION, whiteout_lib
# ============================================================================

# ---- Static library: standalone renderer + MDX adapter ----
add_library(WhiteoutDexRendererLib STATIC
    src/renderer/renderer.cpp
    src/renderer/particle.cpp
    src/renderer/ribbon.cpp
    src/renderer/mdx_animation.cpp
    src/renderer/mdx_model_adapter.cpp
    src/renderer/team_glow_data.cpp
    src/renderer/pe1_system.cpp
    src/renderer/file_resolver.cpp
    src/renderer/file_content_provider.cpp
)

set_target_properties(WhiteoutDexRendererLib PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(WhiteoutDexRendererLib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer"
)

target_link_libraries(WhiteoutDexRendererLib PUBLIC
    whiteout_lib
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    user32.lib
    gdi32.lib
    comdlg32.lib
)

if(TARGET whiteout_casc)
    target_link_libraries(WhiteoutDexRendererLib PUBLIC whiteout_casc)
endif()

if(TARGET whiteout_mpq)
    target_link_libraries(WhiteoutDexRendererLib PUBLIC whiteout_mpq)
endif()

if(MSVC)
    target_compile_options(WhiteoutDexRendererLib PRIVATE
        /W3 /EHsc
        /wd4100 /wd4127 /wd4244 /wd4267
    )
endif()

# ---- Shared library: Max plugin (.dlx) ----
add_library(WhiteoutDexExtractor SHARED
    src/dllmain.cpp
    src/max_scene_adapter.cpp
    src/WhiteoutDexExtractor.def
    src/resource.rc
)

set_target_properties(WhiteoutDexExtractor PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    SUFFIX                ".dlx"
    OUTPUT_NAME           "WhiteoutDexExtractor"
)

target_include_directories(WhiteoutDexExtractor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer"
)

target_compile_definitions(WhiteoutDexExtractor PRIVATE
    NDX_MAX_VERSION=${MAX_VERSION}
)

target_link_libraries(WhiteoutDexExtractor PRIVATE
    WhiteoutDexRendererLib
    MaxSDK
    core.lib
    maxutil.lib
    geom.lib
    mesh.lib
    bmm.lib
    paramblk2.lib
    maxscrpt.lib
)

if(MSVC)
    target_compile_options(WhiteoutDexExtractor PRIVATE
        /W3 /EHsc
        /wd4100 /wd4127 /wd4244 /wd4267
        /permissive
        /Zc:strictStrings-
    )
endif()

# ---- Standalone executable: MDX viewer (no Max SDK) ----
add_executable(WhiteoutDexRendererStandalone
    src/test_main.cpp
    src/resource.rc
)

set_target_properties(WhiteoutDexRendererStandalone PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME           "WhiteoutDexRenderer"
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/standalone"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/standalone"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/standalone"
)

target_link_libraries(WhiteoutDexRendererStandalone PRIVATE
    WhiteoutDexRendererLib
    comdlg32
)

if(MSVC)
    target_compile_options(WhiteoutDexRendererStandalone PRIVATE
        /W3 /EHsc
        /wd4100 /wd4127 /wd4244 /wd4267
    )
endif()