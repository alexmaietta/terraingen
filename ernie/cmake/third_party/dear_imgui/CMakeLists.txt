add_library(
    dear_imgui STATIC
    "${dear_imgui_SOURCE_DIR}/imgui.cpp" "${dear_imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${dear_imgui_SOURCE_DIR}/imgui_draw.cpp" "${dear_imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${dear_imgui_SOURCE_DIR}/imgui_widgets.cpp")
target_include_directories(dear_imgui SYSTEM PUBLIC "${dear_imgui_SOURCE_DIR}")

if(TARGET glfw AND TARGET ernie_volk)
    add_library(dear_imgui_glfw_vulkan STATIC "${dear_imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
                                              "${dear_imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
    target_include_directories(dear_imgui_glfw_vulkan SYSTEM
                               PUBLIC "${dear_imgui_SOURCE_DIR}/backends")
    # Vulkan functions come from volk, as in Ernie, so the backend works with MoltenVK too.
    target_compile_definitions(dear_imgui_glfw_vulkan PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
    target_link_libraries(dear_imgui_glfw_vulkan PUBLIC dear_imgui glfw ernie_volk)
endif()
