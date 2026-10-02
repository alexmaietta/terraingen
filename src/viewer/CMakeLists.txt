add_subdirectory(render)

add_library(terraingen_viewer_app STATIC generation_worker.cpp panels.cpp terrain_settings.cpp
                                         view_settings.cpp viewer_app.cpp)
target_include_directories(terraingen_viewer_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
    terraingen_viewer_app
    PUBLIC terraingen::core terraingen_render
    PRIVATE terraingen_warnings terraingen_coverage)

add_executable(terraingen-viewer main.cpp)
target_link_libraries(terraingen-viewer PRIVATE terraingen_viewer_app terraingen_warnings
                                                terraingen_coverage)

if(TERRAINGEN_INSTALL)
    install(TARGETS terraingen-viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
endif()
