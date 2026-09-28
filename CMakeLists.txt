cmake_minimum_required(VERSION 3.22)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TERRAINGEN_VERSION)

project(
    TerrainGen
    VERSION ${TERRAINGEN_VERSION}
    LANGUAGES CXX)

option(TERRAINGEN_BUILD_CLI "Build the terraingen command-line app" ${PROJECT_IS_TOP_LEVEL})
option(TERRAINGEN_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(TERRAINGEN_BUILD_VIEWER "Build the static terrain viewer" OFF)
option(TERRAINGEN_BUILD_DEMOS "Build the demos" OFF)
option(TERRAINGEN_BUILD_PYTHON "Build the Python bindings" OFF)
option(TERRAINGEN_WITH_GDAL "Build terraingen::io, which requires GDAL" ON)
option(TERRAINGEN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

add_library(terraingen_warnings INTERFACE)
if(MSVC)
    target_compile_options(terraingen_warnings
                           INTERFACE /W4 $<$<BOOL:${TERRAINGEN_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(
        terraingen_warnings
        INTERFACE -Wall
                  -Wextra
                  -Wpedantic
                  -Wshadow
                  -Wconversion
                  -Wsign-conversion
                  -Wdouble-promotion
                  $<$<BOOL:${TERRAINGEN_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

add_subdirectory(src)

if(TERRAINGEN_BUILD_CLI)
    add_subdirectory(apps/cli)
endif()
