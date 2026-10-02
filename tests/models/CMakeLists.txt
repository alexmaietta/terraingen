terraingen_add_test_suite(models SOURCES synthetic_terrain_test.cpp size_frequency_test.cpp
                          LIBRARIES terraingen::core)
terraingen_add_test_suite(
    models_statistical
    SOURCES
    size_frequency_statistical_test.cpp
    LIBRARIES
    terraingen::core
    LABELS
    statistical)
