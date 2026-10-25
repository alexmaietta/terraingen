# SPDX-License-Identifier: BSD-3-Clause

# The `terraingen` command-line app: generate, validate and describe JSON terrain configs.
# The commands live in a library so tests can run them without a process.
add_library(terraingen_cli STATIC cli.cpp)
target_include_directories(terraingen_cli PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>")
target_link_libraries(
    terraingen_cli
    PUBLIC terraingen::terraingen
    PRIVATE terraingen_internal terraingen_warnings terraingen_coverage)

add_executable(terraingen_app main.cpp)
target_link_libraries(terraingen_app PRIVATE terraingen_cli terraingen_warnings terraingen_coverage)
set_target_properties(terraingen_app PROPERTIES OUTPUT_NAME terraingen)

if(TERRAINGEN_INSTALL)
    install(TARGETS terraingen_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
endif()
# In a Python wheel, the command goes in the scripts folder, which pip puts on PATH.
if(SKBUILD)
    install(TARGETS terraingen_app RUNTIME DESTINATION "${SKBUILD_SCRIPTS_DIR}")
endif()
