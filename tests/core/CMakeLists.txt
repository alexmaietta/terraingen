terraingen_add_test_suite(core SOURCES version_test.cpp log_test.cpp LIBRARIES terraingen::core)
target_compile_definitions(terraingen_tests_core
                           PRIVATE TERRAINGEN_EXPECTED_VERSION="${PROJECT_VERSION}")

add_test(
    NAME core.build_info_stable
    COMMAND
        ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTEMPLATE=${PROJECT_SOURCE_DIR}/terraingen/src/core/build_info.cpp.in
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/build_info_stable -P
        "${CMAKE_CURRENT_SOURCE_DIR}/build_info_stable.cmake")
set_tests_properties(core.build_info_stable PROPERTIES LABELS unit)
