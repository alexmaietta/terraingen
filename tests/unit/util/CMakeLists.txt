terraingen_add_test_suite(
    util
    SOURCES
    cells_test.cpp
    log_test.cpp
    lru_cache_test.cpp
    parallel_test.cpp
    surface_test.cpp
    version_test.cpp
    LIBRARIES
    terraingen::terraingen)
target_compile_definitions(terraingen_tests_util
                           PRIVATE TERRAINGEN_EXPECTED_VERSION="${PROJECT_VERSION}")
