terraingen_add_test_suite(core SOURCES version_test.cpp log_test.cpp LIBRARIES
                          terraingen::terraingen)
target_compile_definitions(terraingen_tests_core
                           PRIVATE TERRAINGEN_EXPECTED_VERSION="${PROJECT_VERSION}")
