set(terraingen_shaders
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
foreach(shader ${terraingen_shaders})
    string(TOUPPER "${shader}" shader_upper)
    file(READ "shaders/${shader}.wgsl" TERRAINGEN_${shader_upper}_WGSL)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "shaders/${shader}.wgsl")
endforeach()
configure_file(shaders/embedded_shaders.h.in
               "${CMAKE_CURRENT_BINARY_DIR}/generated/render/embedded_shaders.h" @ONLY)

add_library(
    terraingen_render STATIC
    brdf.cpp
    depth_buffer.cpp
    frustum.cpp
    glfw_surface.cpp
    gpu_context.cpp
    image.cpp
    lod_quadtree.cpp
    orbit_camera.cpp
    poisson_disk.cpp
    shadow_cascades.cpp
    surface_material.cpp
    terrain_renderer.cpp)
target_include_directories(
    terraingen_render
    PUBLIC "${PROJECT_SOURCE_DIR}/src/viewer"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(
    terraingen_render
    PUBLIC terraingen::core wgpu_native glfw dear_imgui_glfw_wgpu terraingen_glm
    PRIVATE terraingen_stb terraingen_tinyexr terraingen_warnings terraingen_coverage)
target_compile_definitions(terraingen_render PRIVATE TERRAINGEN_WEBGPU_WGPU_NATIVE)
if(APPLE)
    set_source_files_properties(glfw_surface.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()
