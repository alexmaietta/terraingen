add_library(ernie_gui STATIC src/panel.cpp src/session.cpp src/theme.cpp src/viewer.cpp)
target_include_directories(
    ernie_gui
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(ernie_gui PUBLIC cxx_std_20)
target_link_libraries(
    ernie_gui
    PUBLIC ernie_backends
    PRIVATE dear_imgui_glfw_vulkan ernie_implot ernie_imguizmo ernie_gui_fonts ernie_stb
            ernie_build_options)
add_library(Ernie::gui ALIAS ernie_gui)

# The GUI's private headers, for its tests.
add_library(ernie_gui_internal INTERFACE)
target_include_directories(ernie_gui_internal INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ernie_gui_internal INTERFACE ernie_gui dear_imgui ernie_stb)
