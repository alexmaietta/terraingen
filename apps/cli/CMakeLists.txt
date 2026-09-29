add_executable(terraingen-cli main.cpp)

target_link_libraries(terraingen-cli PRIVATE terraingen::core terraingen_warnings
                                             terraingen_coverage)

set_target_properties(terraingen-cli PROPERTIES OUTPUT_NAME terraingen)

if(TERRAINGEN_INSTALL)
    install(TARGETS terraingen-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
