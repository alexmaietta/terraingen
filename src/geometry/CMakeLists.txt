# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_library(
    geometry
    SOURCES
    cube_sphere_cells.cpp
    planar_cells.cpp
    plane_fit.cpp
    radial_shape.cpp
    surface.cpp
    PUBLIC
    terraingen_util)
