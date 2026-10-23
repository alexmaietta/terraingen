# SPDX-License-Identifier: BSD-3-Clause

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
    # Every test but core.build_info_stable, which rebuilds objects mid-run and so leaves their counts
    # out of step with their notes.
    add_custom_target(
        coverage
        COMMAND ${CMAKE_COMMAND} -E make_directory "${report_dir}"
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${PROJECT_BINARY_DIR}" --output-on-failure -E
                "^core\\.build_info_stable$"
        COMMAND
            "${GCOVR_EXECUTABLE}" --root "${PROJECT_SOURCE_DIR}" --gcov-executable "${gcov_command}"
            -j 0 --filter "${PROJECT_SOURCE_DIR}/terraingen/" --gcov-exclude ".*build_info.*"
            --gcov-exclude-directories ".*terraingen_build_info\\.dir.*"
            --gcov-ignore-parse-errors=suspicious_hits.warn_once_per_file
            --gcov-ignore-parse-errors=negative_hits.warn_once_per_file
            --merge-mode-functions=separate --exclude-throw-branches --exclude-noncode-lines
            --delete --txt --html-details "${report_dir}/index.html" --html-title
            "TerrainGen coverage" --cobertura "${report_dir}/coverage.xml" --markdown-summary
            "${report_dir}/summary.md" --json-summary "${report_dir}/summary.json" --print-summary
            "${PROJECT_BINARY_DIR}"
        COMMAND
            ${CMAKE_COMMAND} -DSUMMARY=${report_dir}/summary.json -DOUTPUT=${report_dir}/badge.json
            -P "${PROJECT_SOURCE_DIR}/cmake/CoverageBadge.cmake"
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        COMMENT "Running tests and writing coverage reports to ${report_dir}"
        USES_TERMINAL VERBATIM)
    get_property(test_targets GLOBAL PROPERTY TERRAINGEN_TEST_TARGETS)
    add_dependencies(coverage ${test_targets})
endif()
