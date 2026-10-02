terraingen_add_test_suite(distributions SOURCES size_frequency_test.cpp LIBRARIES terraingen::core)
terraingen_add_test_suite(
    distributions_statistical
    SOURCES
    size_frequency_statistical_test.cpp
    LIBRARIES
    terraingen::core
    LABELS
    statistical)
