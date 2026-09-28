include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)

function(terraingen_add_test_suite name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBRARIES")
    set(target terraingen_tests_${name})
    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE ${ARG_LIBRARIES} Catch2::Catch2WithMain
                                            terraingen_warnings)
    catch_discover_tests(${target} TEST_PREFIX "${name}." ADD_TAGS_AS_LABELS DISCOVERY_MODE
                         PRE_TEST)
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
