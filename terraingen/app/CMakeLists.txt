# The `terraingen` command-line app: generate, validate, describe and view JSON terrain configs.
# The commands live in a library so tests can run them without a process.
add_library(terraingen_cli STATIC cli.cpp)
target_link_libraries(
    terraingen_cli
    PUBLIC terraingen::terraingen
    PRIVATE terraingen_warnings terraingen_coverage)

# `terraingen view`: the viewer (viewer/) on Ernie's app when the build has Ernie, else a stub that
# says how to add it.
if(TERRAINGEN_BUILD_VIEWER)
    add_subdirectory(viewer)
    target_sources(terraingen_cli PRIVATE viewer/view.cpp)
    target_link_libraries(terraingen_cli PRIVATE terraingen_viewer)
else()
    target_sources(terraingen_cli PRIVATE viewer/view_none.cpp)
endif()

add_executable(terraingen_app main.cpp)
target_link_libraries(terraingen_app PRIVATE terraingen_cli terraingen_warnings terraingen_coverage)
set_target_properties(terraingen_app PROPERTIES OUTPUT_NAME terraingen)

if(TERRAINGEN_INSTALL)
    install(TARGETS terraingen_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
endif()
