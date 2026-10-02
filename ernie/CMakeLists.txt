cmake_minimum_required(VERSION 3.22)

project(
    Ernie
    VERSION 0.1.0
    DESCRIPTION "Standalone Vulkan terrain renderer"
    LANGUAGES C CXX)

option(ERNIE_BUILD_TESTS "Build Ernie's tests" ${PROJECT_IS_TOP_LEVEL})
option(ERNIE_BUILD_PYTHON "Build Ernie's Python module" OFF)

set(ERNIE_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}"
    CACHE INTERNAL "Ernie source directory")

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
endif()

# The Python module links Ernie's static libraries into a shared object.
if(ERNIE_BUILD_PYTHON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

include(cmake/third_party/vulkan_headers.cmake)
include(cmake/third_party/volk.cmake)
include(cmake/third_party/vma.cmake)
include(cmake/third_party/slang.cmake)
if(APPLE)
    include(cmake/third_party/moltenvk.cmake)
endif()
include(cmake/third_party/glm.cmake)
include(cmake/third_party/dlpack.cmake)
include(cmake/third_party/stb.cmake)
include(cmake/third_party/tinyexr.cmake)

# Warning and coverage flags: TerrainGen's when built inside it, Ernie's own otherwise.
add_library(ernie_build_options INTERFACE)
if(TARGET terraingen_warnings)
    target_link_libraries(ernie_build_options INTERFACE terraingen_warnings terraingen_coverage)
elseif(MSVC)
    target_compile_options(ernie_build_options INTERFACE /W4)
else()
    target_compile_options(
        ernie_build_options
        INTERFACE -Wall
                  -Wextra
                  -Wpedantic
                  -Wshadow
                  -Wconversion
                  -Wsign-conversion
                  -Wdouble-promotion)
endif()

set(ernie_shaders
    material/brdf.slang
    material/lambert.slang
    material/lommel_seeliger.slang
    material/lunar_lambert.slang
    material/oren_nayar.slang
    material/hapke.slang
    raster/filters.slang
    raster/frame.slang
    raster/mesh.slang
    raster/present.slang
    raster/shading.slang
    raster/terrain.slang
    raster/surface.slang
    raster/terrain_shadow.slang
    raster/terrain_color.slang)
foreach(shader ${ernie_shaders})
    string(MAKE_C_IDENTIFIER "${shader}" shader_id)
    string(TOUPPER "${shader_id}" shader_upper)
    file(READ "ernie/src/shaders/${shader}" ERNIE_${shader_upper})
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "ernie/src/shaders/${shader}")
endforeach()
configure_file(ernie/src/shaders/embedded_shaders.h.in
               "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_shaders.h" @ONLY)

# Layers, each depending only on the ones above it:
#   Ernie::core    image I/O, raster views, geometry, cameras, materials, scene, tensors; no Vulkan
#   Ernie::gpu     Vulkan device, memory, pipelines, Slang compiler, presentation
#   Ernie::raster  the rasterizer
#   Ernie::ernie   everything
function(ernie_layer target)
    target_include_directories(
        ${target}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ernie/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ernie/src" "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_link_libraries(${target} PRIVATE ernie_build_options)
endfunction()

add_library(
    ernie_core STATIC
    ernie/src/camera/orbit_camera.cpp
    ernie/src/camera/physical_camera.cpp
    ernie/src/core/image.cpp
    ernie/src/core/spectrum.cpp
    ernie/src/geometry/frustum.cpp
    ernie/src/geometry/lod_quadtree.cpp
    ernie/src/interop/tensor.cpp
    ernie/src/material/brdf.cpp
    ernie/src/scene/path.cpp
    ernie/src/scene/scene.cpp
    ernie/src/scene/schema.cpp)
ernie_layer(ernie_core)
target_link_libraries(
    ernie_core
    PUBLIC ernie_glm ernie_dlpack
    PRIVATE ernie_stb ernie_tinyexr)

add_library(
    ernie_gpu STATIC
    ernie/src/gpu/gpu_buffer.cpp
    ernie/src/gpu/gpu_image.cpp
    ernie/src/interop/cuda_interop.cpp
    ernie/src/gpu/offscreen_target.cpp
    ernie/src/gpu/pipeline.cpp
    ernie/src/gpu/shader_compiler.cpp
    ernie/src/gpu/swapchain.cpp
    ernie/src/gpu/vulkan_context.cpp)
ernie_layer(ernie_gpu)
target_link_libraries(
    ernie_gpu
    PUBLIC ernie_core ernie_volk ernie_vma
    PRIVATE slang::slang ${CMAKE_DL_LIBS})
# Vulkan structs are initialised as `VkFoo info{VK_STRUCTURE_TYPE_FOO}` with the rest zeroed, here
# and in code that uses Ernie's Vulkan types.
target_compile_options(ernie_gpu
                       PUBLIC $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-missing-field-initializers>)
if(APPLE)
    target_compile_definitions(ernie_gpu PRIVATE ERNIE_MOLTENVK_PATH="${ERNIE_MOLTENVK_LIBRARY}")
endif()

add_library(
    ernie_raster STATIC
    ernie/src/raster/mesh_part.cpp
    ernie/src/raster/poisson_disk.cpp
    ernie/src/raster/raster_delegate.cpp
    ernie/src/raster/raster_material.cpp
    ernie/src/raster/raster_renderer.cpp
    ernie/src/raster/render_outputs.cpp
    ernie/src/raster/render_targets.cpp
    ernie/src/raster/shadow_cascades.cpp
    ernie/src/raster/terrain_part.cpp)
ernie_layer(ernie_raster)
target_link_libraries(ernie_raster PUBLIC ernie_gpu)

add_library(ernie INTERFACE)
target_link_libraries(ernie INTERFACE ernie_raster)
add_library(Ernie::core ALIAS ernie_core)
add_library(Ernie::gpu ALIAS ernie_gpu)
add_library(Ernie::raster ALIAS ernie_raster)
add_library(Ernie::ernie ALIAS ernie)

if(ERNIE_BUILD_PYTHON)
    include(cmake/third_party/nanobind.cmake)
    add_subdirectory(ernie/python)
endif()

if(ERNIE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
