terraingen_add_test_suite(
    filters
    SOURCES
    gaussian_blur_filter_test.cpp
    rim_smoothing_test.cpp
    LIBRARIES
    terraingen::filters
    terraingen::layers)
