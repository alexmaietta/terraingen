# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_library(
    sources
    SOURCES
    diamond_square.cpp
    field.cpp
    flat_source.cpp
    geotiff_source.cpp
    raster_source.cpp
    seam_field.cpp
    shape_model_source.cpp
    source_chain.cpp
    synthetic_source.cpp
    upsampling.cpp
    PUBLIC
    terraingen_noise
    terraingen_random
    terraingen_raster
    terraingen_util)
