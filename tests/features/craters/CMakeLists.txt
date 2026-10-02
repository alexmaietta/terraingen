terraingen_add_test_suite(craters SOURCES ages_test.cpp lunar_cratering_test.cpp LIBRARIES
                          terraingen::core)
terraingen_add_test_suite(
    craters_statistical
    SOURCES
    ages_statistical_test.cpp
    lunar_cratering_statistical_test.cpp
    LIBRARIES
    terraingen::core
    LABELS
    statistical)
