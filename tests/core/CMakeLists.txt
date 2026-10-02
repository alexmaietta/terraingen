terraingen_add_test_suite(
    core
    SOURCES
    version_test.cpp
    geo_transform_test.cpp
    raster_test.cpp
    raster_stats_test.cpp
    LIBRARIES
    terraingen::core)
target_compile_definitions(terraingen_tests_core
                           PRIVATE TERRAINGEN_EXPECTED_VERSION="${PROJECT_VERSION}")
