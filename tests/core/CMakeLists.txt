terraingen_add_test_suite(core SOURCES version_test.cpp log_test.cpp LIBRARIES terraingen::core)
target_compile_definitions(terraingen_tests_core
                           PRIVATE TERRAINGEN_EXPECTED_VERSION="${PROJECT_VERSION}")

# Builds the library, so it runs alone to keep its relink away from tests running binaries.
add_test(
    NAME core.build_info_stable
    COMMAND
        ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DBUILD_DIR=${PROJECT_BINARY_DIR}
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR} -P
        "${CMAKE_CURRENT_SOURCE_DIR}/build_info_stable.cmake")
set_tests_properties(core.build_info_stable PROPERTIES LABELS integration RUN_SERIAL TRUE)
