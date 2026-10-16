terraingen_add_test_suite(
    moon
    SOURCES
    mars/surface_test.cpp
    radii_test.cpp
    moon/surface_test.cpp
    LIBRARIES
    terraingen::bodies)
