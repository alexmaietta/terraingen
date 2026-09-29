include("${PROJECT_SOURCE_DIR}/cmake/third_party/googletest.cmake")
include(GoogleTest)

function(terraingen_add_test_suite name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBRARIES;LABELS")
    if(NOT ARG_LABELS)
        set(ARG_LABELS unit)
    endif()
    set(target terraingen_tests_${name})
    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE ${ARG_LIBRARIES} GTest::gtest_main terraingen_warnings
                                            terraingen_coverage)
    set_property(GLOBAL APPEND PROPERTY TERRAINGEN_TEST_TARGETS ${target})
    gtest_discover_tests(
        ${target}
        TEST_PREFIX "${name}." DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "${ARG_LABELS}")
endfunction()

add_subdirectory(core)
add_subdirectory(kernels)
add_subdirectory(models)
if(TARGET terraingen_render)
    add_subdirectory(render)
endif()

if(TERRAINGEN_INSTALL AND NOT TERRAINGEN_COVERAGE)
    set(package_prefix "${CMAKE_CURRENT_BINARY_DIR}/package/prefix")
    add_test(NAME package.install COMMAND ${CMAKE_COMMAND} --install ${PROJECT_BINARY_DIR} --config
                                          $<CONFIG> --prefix ${package_prefix})
    add_test(
        NAME package.consumer
        COMMAND
            ${CMAKE_CTEST_COMMAND} --build-and-test "${CMAKE_CURRENT_SOURCE_DIR}/package"
            "${CMAKE_CURRENT_BINARY_DIR}/package/build" --build-generator ${CMAKE_GENERATOR}
            --build-config $<CONFIG> --build-options "-DCMAKE_PREFIX_PATH=${package_prefix}"
            --test-command package_test)
    set_tests_properties(package.install PROPERTIES FIXTURES_SETUP terraingen_package LABELS
                                                    integration)
    set_tests_properties(package.consumer PROPERTIES FIXTURES_REQUIRED terraingen_package LABELS
                                                     integration)
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
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${PROJECT_BINARY_DIR}" --output-on-failure
        COMMAND
            "${GCOVR_EXECUTABLE}" --root "${PROJECT_SOURCE_DIR}" --gcov-executable "${gcov_command}"
            --filter "${PROJECT_SOURCE_DIR}/include/terraingen/" --filter
            "${PROJECT_SOURCE_DIR}/src/" --filter "${PROJECT_SOURCE_DIR}/apps/" --exclude
            "${PROJECT_SOURCE_DIR}/apps/viewer/" --exclude-throw-branches --delete --txt
            --html-details "${report_dir}/index.html" --cobertura "${report_dir}/coverage.xml"
            --print-summary "${PROJECT_BINARY_DIR}"
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        COMMENT "Running tests and writing coverage reports to ${report_dir}"
        USES_TERMINAL VERBATIM)
    get_property(test_targets GLOBAL PROPERTY TERRAINGEN_TEST_TARGETS)
    add_dependencies(coverage ${test_targets})
endif()
