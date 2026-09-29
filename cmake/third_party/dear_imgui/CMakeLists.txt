add_library(
    dear_imgui STATIC
    "${dear_imgui_SOURCE_DIR}/imgui.cpp" "${dear_imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${dear_imgui_SOURCE_DIR}/imgui_draw.cpp" "${dear_imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${dear_imgui_SOURCE_DIR}/imgui_widgets.cpp")
target_include_directories(dear_imgui SYSTEM PUBLIC "${dear_imgui_SOURCE_DIR}")

if(TARGET glfw AND TARGET wgpu_native)
    add_library(dear_imgui_glfw_wgpu STATIC "${dear_imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
                                            "${dear_imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp")
    target_include_directories(dear_imgui_glfw_wgpu SYSTEM
                               PUBLIC "${dear_imgui_SOURCE_DIR}/backends")
    # ImGui's WGPU path targets pre-v27 wgpu-native; its DAWN path targets the standard webgpu.h
    # that current wgpu-native and browsers implement.
    target_compile_definitions(dear_imgui_glfw_wgpu PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
    target_link_libraries(dear_imgui_glfw_wgpu PUBLIC dear_imgui glfw wgpu_native)
    if(APPLE)
        set_source_files_properties("${dear_imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp"
                                    PROPERTIES COMPILE_FLAGS "-x objective-c++")
    endif()
endif()
