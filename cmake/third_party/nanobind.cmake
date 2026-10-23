# SPDX-License-Identifier: BSD-3-Clause

set(TERRAINGEN_DEP_VERSION_nanobind
    v3.1.0
    CACHE STRING "Tag of nanobind to fetch when it is not installed for the target Python.")
mark_as_advanced(TERRAINGEN_DEP_VERSION_nanobind)

if(NOT DEFINED Python_EXECUTABLE AND EXISTS "${PROJECT_SOURCE_DIR}/.venv/bin/python")
    set(Python_EXECUTABLE "${PROJECT_SOURCE_DIR}/.venv/bin/python")
endif()
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module
                                             ${SKBUILD_SABI_COMPONENT})

execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_VARIABLE nanobind_ROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
find_package(nanobind CONFIG QUIET)

if(NOT nanobind_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nanobind
        GIT_REPOSITORY https://github.com/wjakob/nanobind.git
        GIT_TAG ${TERRAINGEN_DEP_VERSION_nanobind}
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ext/robin_map)
    FetchContent_MakeAvailable(nanobind)
endif()
