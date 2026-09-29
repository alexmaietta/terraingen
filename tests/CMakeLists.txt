include(FetchContent)
include(GoogleTest)

set(BUILD_GMOCK OFF)
set(INSTALL_GTEST OFF)
set(gtest_force_shared_crt ON)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(googletest)

function(terraingen_add_test_suite name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBRARIES;LABELS")
    if(NOT ARG_LABELS)
        set(ARG_LABELS unit)
    endif()
    set(target terraingen_tests_${name})
    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE ${ARG_LIBRARIES} GTest::gtest_main terraingen_warnings)
    gtest_discover_tests(
        ${target}
        TEST_PREFIX "${name}." DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "${ARG_LABELS}")
endfunction()

add_subdirectory(core)

if(TERRAINGEN_INSTALL)
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
