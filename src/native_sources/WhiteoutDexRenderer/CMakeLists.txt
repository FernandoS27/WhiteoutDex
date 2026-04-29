# ============================================================================
# WhiteoutDexRenderer — Two targets:
#   1. WhiteoutDexRendererLib (STATIC) — standalone renderer, no Max SDK
#   2. WhiteoutDexExtractor   (SHARED .dlx) — Max plugin linking the renderer
# Parent provides: MaxSDK (interface lib), MAX_VERSION, whiteout_lib
# ============================================================================

# ============================================================================
# Slang shader compilation (compile-time: .slang → DXBC → embedded C++ header)
# ============================================================================

set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/shaders")
set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

find_program(SLANGC slangc
    HINTS "${SLANG_DIR}/bin" "$ENV{SLANG_DIR}/bin"
    DOC   "Path to Slang compiler (slangc)")

if(NOT SLANGC)
    message(FATAL_ERROR
        "slangc not found. Install the Slang SDK from https://github.com/shader-slang/slang/releases "
        "and set -DSLANG_DIR=<path-to-slang-sdk> or add slangc to PATH.")
endif()
message(STATUS "Using slangc: ${SLANGC}")

# Common Slang flags: column-major matrices (matches C++ transpose-before-upload convention)
# Use DXC as the downstream compiler instead of FXC for DXBC output
set(SLANG_COMMON_FLAGS -matrix-layout-column-major -I "${SHADER_SRC_DIR}"
    -default-downstream-compiler hlsl dxc)

# Shader entry points: <slang_file>  <entry>  <stage>  <output_name>
# Only line.slang (debug wires) and viewcube.slang (debug orientation
# cube) remain as Slang sources; every MDX-material draw goes through
# BLS, and the tonemap pass uses the engine's sprite.bls VS directly.
set(SHADER_ENTRIES
    "line.slang|vsLine|vertex|kLineVS"
    "line.slang|psLine|fragment|kLinePS"
    "viewcube.slang|vsViewCube|vertex|kViewCubeVS"
    "viewcube.slang|psViewCube|fragment|kViewCubePS"
)

set(SHADER_DXBC_FILES "")
set(SHADER_DXBC_NAMES "")

foreach(entry IN LISTS SHADER_ENTRIES)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 src_file)
    list(GET parts 1 entry_name)
    list(GET parts 2 stage)
    list(GET parts 3 var_name)

    set(dxbc_path "${SHADER_OUT_DIR}/${var_name}.dxbc")

    add_custom_command(
        OUTPUT  "${dxbc_path}"
        COMMAND "${SLANGC}" "${SHADER_SRC_DIR}/${src_file}"
                ${SLANG_COMMON_FLAGS}
                -entry ${entry_name} -stage ${stage}
                -target dxbc -profile sm_5_0
                -o "${dxbc_path}"
        DEPENDS "${SHADER_SRC_DIR}/${src_file}"
                "${SHADER_SRC_DIR}/common.slang"
        COMMENT "Slang: ${src_file} [${entry_name}] → ${var_name}.dxbc"
        VERBATIM
    )

    list(APPEND SHADER_DXBC_FILES "${dxbc_path}")
    list(APPEND SHADER_DXBC_NAMES "${var_name}")
endforeach()

# Embed all DXBC blobs into a single C++ header
set(COMPILED_SHADERS_H "${SHADER_OUT_DIR}/compiled_shaders.h")

add_custom_command(
    OUTPUT  "${COMPILED_SHADERS_H}"
    COMMAND "${CMAKE_COMMAND}"
            "-DSHADER_DIR=${SHADER_OUT_DIR}"
            "-DOUTPUT=${COMPILED_SHADERS_H}"
            -P "${SHADER_SRC_DIR}/embed_shaders.cmake"
    DEPENDS ${SHADER_DXBC_FILES}
            "${SHADER_SRC_DIR}/embed_shaders.cmake"
    COMMENT "Embedding compiled shaders into compiled_shaders.h"
    VERBATIM
)

add_custom_target(WhiteoutDexShaders DEPENDS "${COMPILED_SHADERS_H}")

# ---- Static library: GFX Service (backend-agnostic abstraction) ----
add_library(WhiteoutDexGfx STATIC
    src/gfx/gfx_factory.cpp
    src/gfx/d3d11/d3d11_device.cpp
    src/gfx/d3d11/d3d11_command_list.cpp
    src/gfx/d3d12/d3d12_device.cpp
    src/gfx/d3d12/d3d12_command_list.cpp
    src/gfx/d3d12/d3d12_resources.cpp
)

set_target_properties(WhiteoutDexGfx PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(WhiteoutDexGfx PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(WhiteoutDexGfx
    PRIVATE d3d11.lib d3d12.lib dxgi.lib
)

if(MSVC)
    target_compile_options(WhiteoutDexGfx PRIVATE
        /W3 /EHsc
        /wd4100 /wd4127 /wd4244 /wd4267
    )
endif()

# ---- Renderer-native coordinate system (compile-time knob) ----
set(WDX_DEFAULT_COORD_SPACE "Blizzard" CACHE STRING
    "Renderer-native coordinate system. Valid values: Blizzard, Max.")
set_property(CACHE WDX_DEFAULT_COORD_SPACE PROPERTY STRINGS "Blizzard" "Max")

# ---- Static library: standalone renderer + MDX adapter ----
add_library(WhiteoutDexRendererLib STATIC
    src/renderer/render_service.cpp
    src/renderer/render_service_internal.cpp
    src/renderer/sampler_asset_manager.cpp
    src/renderer/texture_asset_manager.cpp
    src/renderer/replaceable_texture_manager.cpp
    src/renderer/model_template.cpp
    src/renderer/model_template_manager.cpp
    src/renderer/scene_manager.cpp
    src/renderer/animation_driver.cpp
    src/renderer/render_model.cpp
    src/renderer/render_target.cpp
    src/renderer/viewcube_atlas.cpp
    src/renderer/coordinate_system.cpp
    src/renderer/debug/debug_renderer.cpp
    src/ui/render_window.cpp
    src/renderer/ribbon.cpp
    src/renderer/pe1_system.cpp
    src/renderer/particle/rnd_seed.cpp
    src/renderer/particle/particle_key.cpp
    src/renderer/particle/particle_pool.cpp
    src/renderer/particle/particle2_emitter.cpp
    src/renderer/particle/plane_emitter.cpp
    src/renderer/particle/particle_geometry.cpp
    src/renderer/particle/particle_service.cpp
    src/renderer/particle/splat_service.cpp
    src/renderer/event_emitter_pool.cpp
    src/renderer/sound_service.cpp
    src/renderer/spn_spawner.cpp
    src/renderer/bls/bls_container.cpp
    src/renderer/bls/bls_shader_cache.cpp
    src/renderer/bls/bls_permuter.cpp
    src/renderer/bls/bls_program.cpp
    src/renderer/bls/bls_pso_builder.cpp
    src/renderer/bls/bls_mat_params.cpp
    src/renderer/bls/bls_frame.cpp
    src/renderer/bls/bls_draw_helpers.cpp
    src/renderer/ibl/split_sum.cpp
    src/renderer/ibl/env_probe.cpp
    src/io/mdx_animation.cpp
    src/io/mdx_model_adapter.cpp
    src/io/team_glow_data.cpp
    src/io/file_resolver.cpp
    src/io/file_content_provider.cpp
    src/io/replaceable_paths.cpp
    src/io/event_data.cpp
    src/io/slk.cpp
    src/io/texture_image_usage.cpp
)

add_dependencies(WhiteoutDexRendererLib WhiteoutDexShaders)

set_target_properties(WhiteoutDexRendererLib PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(WhiteoutDexRendererLib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
    "${SHADER_OUT_DIR}"
)

target_link_libraries(WhiteoutDexRendererLib PUBLIC
    whiteout_lib
    WhiteoutDexGfx
    user32.lib
    gdi32.lib
    comdlg32.lib
)

target_compile_definitions(WhiteoutDexRendererLib PUBLIC
    WDX_DEFAULT_COORD_SPACE=${WDX_DEFAULT_COORD_SPACE}
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
    OUTPUT_NAME           "WhiteoutFlakes"
)

target_include_directories(WhiteoutDexExtractor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
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

# ---- Throwaway: dump a BLS permute's disassembly ----
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/bls/bls_reflect_tool.cpp")
  add_executable(BlsReflect
      src/renderer/bls/bls_reflect_tool.cpp
      src/renderer/bls/bls_container.cpp
  )
  set_target_properties(BlsReflect PROPERTIES
      CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
      RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/standalone"
      RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/standalone"
  )
  target_include_directories(BlsReflect PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer")
  target_link_libraries(BlsReflect PRIVATE d3d11.lib d3dcompiler.lib)
  if(MSVC)
    target_compile_options(BlsReflect PRIVATE /W3 /EHsc /wd4100 /wd4127 /wd4244 /wd4267)
  endif()
endif()

# ---- Standalone executable: MDX viewer (no Max SDK) ----
add_executable(WhiteoutDexRendererStandalone
    src/test_main.cpp
    src/resource.rc
)

set_target_properties(WhiteoutDexRendererStandalone PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME           "WhiteoutFlakes"
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