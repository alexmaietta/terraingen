# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_library(
    pipeline
    SOURCES
    body_mesh.cpp
    materials.cpp
    mesh_build.cpp
    report.cpp
    terrain.cpp
    PUBLIC
    terraingen_analysis
    terraingen_distribution
    terraingen_geometry
    terraingen_layers
    terraingen_mesh
    terraingen_raster
    terraingen_sources
    terraingen_util)
