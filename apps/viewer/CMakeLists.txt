add_library(terraingen_viewer_app STATIC app.cpp log_buffer.cpp options.cpp panels.cpp settings.cpp
                                         worker.cpp)
target_include_directories(terraingen_viewer_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
    terraingen_viewer_app
    PUBLIC terraingen::core Ernie::gui
    PRIVATE terraingen_warnings terraingen_coverage)

add_executable(terraingen-viewer main.cpp)
target_link_libraries(terraingen-viewer PRIVATE terraingen_viewer_app terraingen_warnings
                                                terraingen_coverage)

if(TERRAINGEN_INSTALL)
    # Ernie compiles shaders at run time with Slang, whose library ships privately beside the
    # viewer.
    set_target_properties(terraingen-viewer
                          PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/terraingen")
    install(TARGETS terraingen-viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
    install(
        IMPORTED_RUNTIME_ARTIFACTS
        slang::slang
        LIBRARY
        DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/terraingen
        COMPONENT
        runtime)
endif()
