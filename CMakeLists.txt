cmake_minimum_required(VERSION 3.22)

# Archives fetched with FetchContent get extraction-time timestamps, so their files rebuild what
# depends on them; CMake before 3.24 has no such policy and behaves this way already.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# The release being worked towards, such as 0.1.0, or for a development build a PEP 440 pre-release of
# it, such as 0.1.0.dev42, which only the Python package carries.
set(TERRAINGEN_FULL_VERSION "0.2.0")
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" TERRAINGEN_VERSION "${TERRAINGEN_FULL_VERSION}")

project(
    TerrainGen
    VERSION ${TERRAINGEN_VERSION}
    LANGUAGES C CXX)
# The Metal backend (terraingen/gpu/metal.cpp) is Objective-C++ on Apple platforms. It is enabled
# here, above every target that links the gpu module.
if(APPLE)
    enable_language(OBJCXX)
endif()

# No fused multiply-adds unless the source asks: GCC and Clang contract a * b + c into one rounding on
# CPUs that have the instruction (ARM, Apple Silicon), so the same seed would give different bits
# there than on x86.
add_compile_options(
    "$<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-ffp-contract=off>"
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-ffp-contract=off>"
    "$<$<COMPILE_LANG_AND_ID:OBJCXX,AppleClang,Clang>:-ffp-contract=off>")

option(TERRAINGEN_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(TERRAINGEN_BUILD_BENCHMARKS "Build the benchmarks (Google Benchmark)"
       ${PROJECT_IS_TOP_LEVEL})
option(TERRAINGEN_BUILD_EXAMPLES "Build the C++ examples and test every example"
       ${PROJECT_IS_TOP_LEVEL})
option(TERRAINGEN_BUILD_PYTHON "Build the Python bindings" ON)
option(TERRAINGEN_BUILD_APP "Build the terraingen command-line app" ON)
option(TERRAINGEN_BUILD_GUI
       "Build `terraingen --gui`, the 3D GUI, with Ernie from TERRAINGEN_ERNIE_DIR, if it is there"
       ON)
option(TERRAINGEN_INSTALL "Generate install rules and the CMake package" ${PROJECT_IS_TOP_LEVEL})
option(TERRAINGEN_COVERAGE "Instrument TerrainGen for code coverage and add a coverage target" OFF)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
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
    # -O1 runs the terrain-generating tests several times faster than -O0. The counters are not
    # atomic: atomic ones made threaded tests about three times slower, a race only loses hits, and
    # ccache does not cache compiles with -fprofile-update.
    set(TERRAINGEN_COVERAGE_OPTIMIZATION
        "-O1"
        CACHE STRING "Optimization flag for coverage builds")
    target_compile_options(terraingen_coverage INTERFACE --coverage
                                                         ${TERRAINGEN_COVERAGE_OPTIMIZATION} -g)
    target_link_options(terraingen_coverage INTERFACE --coverage)
endif()

if(TERRAINGEN_BUILD_PYTHON)
    include("${PROJECT_SOURCE_DIR}/cmake/third_party/nanobind.cmake")
endif()

include("${PROJECT_SOURCE_DIR}/cmake/third_party/vulkan_headers.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/third_party/volk.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/third_party/slang.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/third_party/nvrtc.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/TerrainGenSlang.cmake")

# TERRAINGEN_GUI says whether the GUI is built: asked for, with the app, and Ernie found.
set(TERRAINGEN_GUI OFF)
if(TERRAINGEN_BUILD_GUI AND TERRAINGEN_BUILD_APP)
    include("${PROJECT_SOURCE_DIR}/cmake/third_party/ernie.cmake")
endif()

add_subdirectory(terraingen)

if(TERRAINGEN_BUILD_TESTS)
    enable_testing()
    include("${PROJECT_SOURCE_DIR}/cmake/TerrainGenTesting.cmake")
endif()
if(TERRAINGEN_BUILD_EXAMPLES)
    add_subdirectory(examples/cpp)
endif()
if(TERRAINGEN_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(TERRAINGEN_BUILD_BENCHMARKS)
    add_subdirectory(tests/benchmark)
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
