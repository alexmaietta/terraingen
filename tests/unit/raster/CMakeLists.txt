# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_test_suite(
    raster
    SOURCES
    cell_loops_test.cpp
    geo_transform_test.cpp
    geotiff_test.cpp
    grid_test.cpp
    raster_test.cpp
    resample_test.cpp
    tiff_format_test.cpp
    LIBRARIES
    terraingen::raster
    terraingen::random)
