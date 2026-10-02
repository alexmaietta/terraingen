terraingen_add_test_suite(random SOURCES random_test.cpp LIBRARIES terraingen::core)
terraingen_add_test_suite(
    random_statistical
    SOURCES
    random_statistical_test.cpp
    LIBRARIES
    terraingen::core
    LABELS
    statistical)
