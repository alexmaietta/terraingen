# Ernie, the renderer `terraingen --gui` draws with, built from a local checkout inside this build.
# Fetching a release of Ernie will replace TERRAINGEN_ERNIE_DIR once Ernie publishes one.

set(TERRAINGEN_ERNIE_DIR
    ""
    CACHE PATH "Ernie source checkout to build the GUI with (TERRAINGEN_BUILD_GUI)")

if(NOT TERRAINGEN_ERNIE_DIR)
    message(FATAL_ERROR "TERRAINGEN_BUILD_GUI needs TERRAINGEN_ERNIE_DIR, the path of a local "
                        "Ernie checkout, such as -DTERRAINGEN_ERNIE_DIR=../ernie")
endif()
get_filename_component(terraingen_ernie_dir "${TERRAINGEN_ERNIE_DIR}" ABSOLUTE BASE_DIR
                       "${PROJECT_SOURCE_DIR}")
if(NOT EXISTS "${terraingen_ernie_dir}/CMakeLists.txt" OR NOT EXISTS
                                                          "${terraingen_ernie_dir}/ernie/app/viewer.h")
    message(FATAL_ERROR "TERRAINGEN_ERNIE_DIR (${terraingen_ernie_dir}) is not an Ernie checkout")
endif()

# Only the library and its app (its viewer); Ernie's tests, demos, Python module and packages stay
# in Ernie's own builds.
set(ERNIE_BUILD_APP ON CACHE BOOL "" FORCE)
set(ERNIE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ERNIE_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(ERNIE_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(ERNIE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${terraingen_ernie_dir}" "${PROJECT_BINARY_DIR}/ernie" EXCLUDE_FROM_ALL)
message(STATUS "TerrainGen GUI: Ernie from ${terraingen_ernie_dir}")
