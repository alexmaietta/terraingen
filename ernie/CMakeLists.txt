cmake_minimum_required(VERSION 3.22)

project(
    Ernie
    VERSION 0.1.0
    DESCRIPTION "Standalone WebGPU terrain renderer"
    LANGUAGES CXX)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
    set(hint "On Ubuntu 22.04, install g++-13 from ppa:ubuntu-toolchain-r/test.")
    if(NOT PROJECT_IS_TOP_LEVEL)
        string(APPEND hint
               " Configure with -DTERRAINGEN_BUILD_VIEWER=OFF to build TerrainGen without "
               "Ernie and the viewer.")
    endif()
    message(FATAL_ERROR "Ernie needs GCC 13 or newer; found ${CMAKE_CXX_COMPILER_VERSION}. ${hint}")
endif()

option(ERNIE_BUILD_TESTS "Build Ernie's tests" ${PROJECT_IS_TOP_LEVEL})

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

include(cmake/third_party/wgpu_native.cmake)
include(cmake/third_party/glm.cmake)
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
    terrain_common
    terrain
    terrain_shadow
    surface
    brdf_common
    brdf_lambert
    brdf_lommel_seeliger
    brdf_lunar_lambert
    brdf_oren_nayar
    brdf_hapke)
foreach(shader ${ernie_shaders})
    string(TOUPPER "${shader}" shader_upper)
    file(READ "shaders/${shader}.wgsl" ERNIE_${shader_upper}_WGSL)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "shaders/${shader}.wgsl")
endforeach()
configure_file(shaders/embedded_shaders.h.in
               "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_shaders.h" @ONLY)

add_library(
    ernie STATIC
    src/brdf.cpp
    src/depth_buffer.cpp
    src/frustum.cpp
    src/gpu_context.cpp
    src/image.cpp
    src/lod_quadtree.cpp
    src/orbit_camera.cpp
    src/poisson_disk.cpp
    src/shadow_cascades.cpp
    src/surface_material.cpp
    src/terrain_renderer.cpp)
add_library(Ernie::ernie ALIAS ernie)
target_include_directories(
    ernie
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(ernie PUBLIC cxx_std_20)
target_link_libraries(
    ernie
    PUBLIC wgpu_native ernie_glm
    PRIVATE ernie_stb ernie_tinyexr ernie_build_options)
target_compile_definitions(ernie PRIVATE ERNIE_WEBGPU_WGPU_NATIVE)

if(ERNIE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
