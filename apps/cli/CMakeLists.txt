add_library(terraingen_cli_app STATIC cli.cpp)
target_include_directories(terraingen_cli_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
    terraingen_cli_app
    PUBLIC terraingen::pipeline
    PRIVATE terraingen_warnings terraingen_coverage)

add_executable(terraingen-cli main.cpp)
target_link_libraries(terraingen-cli PRIVATE terraingen_cli_app terraingen_warnings
                                             terraingen_coverage)
set_target_properties(terraingen-cli PROPERTIES OUTPUT_NAME terraingen)

if(TERRAINGEN_INSTALL)
    install(TARGETS terraingen-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
endif()
