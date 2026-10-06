cmake_minimum_required(VERSION 3.22)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TERRAINGEN_VERSION)

project(
    TerrainGen
    VERSION ${TERRAINGEN_VERSION}
    LANGUAGES C CXX)

option(TERRAINGEN_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(TERRAINGEN_BUILD_VALIDATION "Build the validation tests against external references"
       ${PROJECT_IS_TOP_LEVEL})
option(TERRAINGEN_BUILD_DEMOS "Build the demos" OFF)
option(TERRAINGEN_BUILD_PYTHON "Build the Python bindings" ON)
option(TERRAINGEN_WITH_GDAL "Reserved for GDAL-backed formats; GeoTIFF needs no GDAL" ON)
option(TERRAINGEN_INSTALL "Generate install rules and the CMake package" ${PROJECT_IS_TOP_LEVEL})
option(TERRAINGEN_COVERAGE "Instrument TerrainGen for code coverage and add a coverage target" OFF)

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
    target_compile_options(terraingen_warnings INTERFACE /W4)
else()
    target_compile_options(
        terraingen_warnings
        INTERFACE -Wall
                  -Wextra
                  -Wpedantic
                  -Wshadow
                  -Wconversion
                  -Wsign-conversion
                  -Wdouble-promotion)
endif()

add_library(terraingen_coverage INTERFACE)
if(TERRAINGEN_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "TERRAINGEN_COVERAGE requires GCC or Clang")
    endif()
    if(NOT TERRAINGEN_BUILD_TESTS)
        message(FATAL_ERROR "TERRAINGEN_COVERAGE requires TERRAINGEN_BUILD_TESTS")
    endif()
    # Atomic counters keep hit counts exact in code that runs on several threads.
    target_compile_options(terraingen_coverage INTERFACE --coverage -fprofile-update=atomic -O0 -g)
    target_link_options(terraingen_coverage INTERFACE --coverage)
endif()

if(TERRAINGEN_BUILD_PYTHON)
    include("${PROJECT_SOURCE_DIR}/cmake/third_party/nanobind.cmake")
endif()

add_subdirectory(terraingen/src)

if(TERRAINGEN_BUILD_PYTHON)
    set_target_properties(terraingen_core terraingen_core_objects
                          PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_subdirectory(terraingen/python)
endif()

if(TERRAINGEN_BUILD_TESTS OR TERRAINGEN_BUILD_VALIDATION)
    enable_testing()
    include("${PROJECT_SOURCE_DIR}/cmake/TerrainGenTesting.cmake")
endif()
if(TERRAINGEN_BUILD_VALIDATION)
    add_subdirectory(validation/checks)
endif()
if(TERRAINGEN_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(TERRAINGEN_INSTALL)
    include(CMakePackageConfigHelpers)
    set(TERRAINGEN_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/terraingen")

    install(
        EXPORT terraingenTargets
        NAMESPACE terraingen::
        DESTINATION "${TERRAINGEN_INSTALL_CMAKEDIR}"
        COMPONENT development)

    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/terraingenConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/terraingenConfig.cmake"
        INSTALL_DESTINATION "${TERRAINGEN_INSTALL_CMAKEDIR}")
    write_basic_package_version_file("${PROJECT_BINARY_DIR}/terraingenConfigVersion.cmake"
                                     COMPATIBILITY SameMinorVersion)

    install(
        FILES "${PROJECT_BINARY_DIR}/terraingenConfig.cmake"
              "${PROJECT_BINARY_DIR}/terraingenConfigVersion.cmake"
        DESTINATION "${TERRAINGEN_INSTALL_CMAKEDIR}"
        COMPONENT development)

    include("${PROJECT_SOURCE_DIR}/cmake/TerrainGenPackaging.cmake")
endif()
