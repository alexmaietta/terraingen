# SPDX-License-Identifier: BSD-3-Clause

execute_process(
    COMMAND "${Python_EXECUTABLE}" -c "import pytest, numpy"
    RESULT_VARIABLE python_test_deps_missing
    OUTPUT_QUIET ERROR_QUIET)
if(python_test_deps_missing)
    message(WARNING "pytest or numpy not found for ${Python_EXECUTABLE}; skipping Python tests")
    return()
endif()

add_test(NAME python.pytest COMMAND "${Python_EXECUTABLE}" -m pytest -q
                                    "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(python.pytest PROPERTIES ENVIRONMENT "PYTHONPATH=${PROJECT_BINARY_DIR}/python"
                                              LABELS "unit;python")
