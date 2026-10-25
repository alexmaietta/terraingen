# SPDX-License-Identifier: BSD-3-Clause

include("${PROJECT_SOURCE_DIR}/cmake/third_party/googletest.cmake")
include(GoogleTest)

function(terraingen_add_test_suite name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBRARIES;LABELS")
    if(NOT ARG_LABELS)
        set(ARG_LABELS unit)
    endif()
    set(target terraingen_tests_${name})
    add_executable(${target} ${ARG_SOURCES})
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(${target} PRIVATE ${ARG_LIBRARIES} terraingen_internal GTest::gtest_main
                                            terraingen_warnings terraingen_coverage)
    set_property(GLOBAL APPEND PROPERTY TERRAINGEN_TEST_TARGETS ${target})
    gtest_discover_tests(
        ${target}
        TEST_PREFIX "${name}." DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "${ARG_LABELS}")
endfunction()
