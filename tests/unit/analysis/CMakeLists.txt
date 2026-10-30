# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_test_suite(
    analysis
    SOURCES
    raster_stats_test.cpp
    spectrum_test.cpp
    variogram_test.cpp
    LIBRARIES
    terraingen::analysis
    terraingen::random)
