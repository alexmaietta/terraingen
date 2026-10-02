add_library(
    terraingen_viewer_app STATIC
    app.cpp
    glfw_surface.cpp
    log_buffer.cpp
    options.cpp
    panels.cpp
    settings.cpp
    worker.cpp)
target_include_directories(terraingen_viewer_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
    terraingen_viewer_app
    PUBLIC terraingen::core Ernie::ernie glfw dear_imgui_glfw_wgpu
    PRIVATE terraingen_warnings terraingen_coverage)
if(APPLE)
    set_source_files_properties(glfw_surface.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()

add_executable(terraingen-viewer main.cpp)
target_link_libraries(terraingen-viewer PRIVATE terraingen_viewer_app terraingen_warnings
                                                terraingen_coverage)

if(TERRAINGEN_INSTALL)
    install(TARGETS terraingen-viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
endif()
