add_library(terraingen_render STATIC gpu_context.cpp glfw_surface.cpp)
target_include_directories(terraingen_render PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(
    terraingen_render
    PUBLIC wgpu_native glfw dear_imgui_glfw_wgpu
    PRIVATE terraingen_warnings terraingen_coverage)
target_compile_definitions(terraingen_render PRIVATE TERRAINGEN_WEBGPU_WGPU_NATIVE)
if(APPLE)
    set_source_files_properties(glfw_surface.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()
