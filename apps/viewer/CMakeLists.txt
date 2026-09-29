add_executable(terraingen-viewer main.cpp)
target_link_libraries(terraingen-viewer PRIVATE terraingen::core terraingen_render
                                                terraingen_warnings terraingen_coverage)

if(TERRAINGEN_INSTALL)
    install(TARGETS terraingen-viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
