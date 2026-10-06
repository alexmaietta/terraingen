# Tests that compare outputs bit for bit with recorded values, to catch unintended changes.
terraingen_add_test_suite(
    noise_golden
    SOURCES
    noise/kernels_test.cpp
    LIBRARIES
    terraingen::core
    LABELS
    golden)
