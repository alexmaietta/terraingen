# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_library(
    analysis
    SOURCES
    spectrum.cpp
    variogram.cpp
    PUBLIC
    terraingen_geometry
    terraingen_raster
    terraingen_util)
