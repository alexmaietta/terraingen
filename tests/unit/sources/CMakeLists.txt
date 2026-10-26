# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_test_suite(
    sources
    SOURCES
    diamond_square_test.cpp
    flat_source_test.cpp
    geotiff_source_test.cpp
    raster_source_test.cpp
    shape_model_source_test.cpp
    source_chain_test.cpp
    synthetic_source_test.cpp
    terrain_base_test.cpp
    upsampling_test.cpp
    LIBRARIES
    terraingen::sources)
