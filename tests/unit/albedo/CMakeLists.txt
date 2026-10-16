terraingen_add_test_suite(
    albedo
    SOURCES
    albedo_test.cpp
    crater_albedo_test.cpp
    LIBRARIES
    terraingen::albedo
    terraingen::layers)
