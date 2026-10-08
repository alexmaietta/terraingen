# The `terraingen` command-line app: generate, validate and describe JSON terrain configs. The
# commands live in a library so tests can run them without a process.
add_library(terraingen_cli STATIC cli.cpp)
target_link_libraries(
    terraingen_cli
    PUBLIC terraingen::terraingen
    PRIVATE terraingen_warnings terraingen_coverage)

add_executable(terraingen_app main.cpp)
target_link_libraries(terraingen_app PRIVATE terraingen_cli terraingen_warnings terraingen_coverage)
set_target_properties(terraingen_app PROPERTIES OUTPUT_NAME terraingen)

if(TERRAINGEN_INSTALL)
    install(TARGETS terraingen_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
endif()
