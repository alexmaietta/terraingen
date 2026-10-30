# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_library(
    albedo
    SOURCES
    albedo.cpp
    bands.cpp
    blast_zones.cpp
    crater_albedo.cpp
    crater_albedo_options.cpp
    maturity.cpp
    photometry.cpp
    raster_albedo.cpp
    rock_albedo.cpp
    slope_albedo.cpp
    splotches.cpp
    synthetic.cpp
    zone_shadows.cpp
    PUBLIC
    terraingen_analysis
    terraingen_geometry
    terraingen_layers
    terraingen_noise
    terraingen_random
    terraingen_raster
    terraingen_sources
    terraingen_util)
