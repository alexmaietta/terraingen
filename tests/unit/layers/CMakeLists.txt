# Some tests use the lunar models as realistic inputs, so the suite links terraingen::bodies too.
terraingen_add_test_suite(
    craters
    SOURCES
    craters/ages/fresh_test.cpp
    craters/ages/uniform_test.cpp
    craters/apply_test.cpp
    craters/degradation/freshness_test.cpp
    craters/degradation/none_test.cpp
    craters/degradation/state_test.cpp
    craters/filters_test.cpp
    craters/population_test.cpp
    craters/profiles/fassett_thomson_test.cpp
    craters/profiles/martin_parkes_dunstan_test.cpp
    LIBRARIES
    terraingen::bodies)
