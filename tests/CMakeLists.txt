add_subdirectory(unit)
add_subdirectory(statistical)
add_subdirectory(golden)
add_subdirectory(integration)
if(TARGET _terraingen)
    add_subdirectory(python)
endif()

if(TERRAINGEN_COVERAGE)
    find_program(GCOVR_EXECUTABLE gcovr HINTS "${PROJECT_SOURCE_DIR}/.venv/bin")
    if(NOT GCOVR_EXECUTABLE)
        message(FATAL_ERROR "TERRAINGEN_COVERAGE requires gcovr (pip install gcovr)")
    endif()

    string(REGEX MATCH "^[0-9]+" compiler_major "${CMAKE_CXX_COMPILER_VERSION}")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        find_program(GCOV_EXECUTABLE NAMES gcov-${compiler_major} gcov REQUIRED)
        set(gcov_command "${GCOV_EXECUTABLE}")
    else()
        find_program(LLVM_COV_EXECUTABLE NAMES llvm-cov-${compiler_major} llvm-cov REQUIRED)
        set(gcov_command "${LLVM_COV_EXECUTABLE} gcov")
    endif()

    set(report_dir "${PROJECT_BINARY_DIR}/coverage")
    add_custom_target(
        coverage
        COMMAND ${CMAKE_COMMAND} -E make_directory "${report_dir}"
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${PROJECT_BINARY_DIR}" --output-on-failure -LE
                validation
        COMMAND
            "${GCOVR_EXECUTABLE}" --root "${PROJECT_SOURCE_DIR}" --gcov-executable "${gcov_command}"
            --filter "${PROJECT_SOURCE_DIR}/terraingen/" --exclude-throw-branches
            --exclude-noncode-lines --delete --txt --html-details "${report_dir}/index.html"
            --cobertura "${report_dir}/coverage.xml" --print-summary "${PROJECT_BINARY_DIR}"
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        COMMENT "Running tests and writing coverage reports to ${report_dir}"
        USES_TERMINAL VERBATIM)
    get_property(test_targets GLOBAL PROPERTY TERRAINGEN_TEST_TARGETS)
    add_dependencies(coverage ${test_targets})
endif()
