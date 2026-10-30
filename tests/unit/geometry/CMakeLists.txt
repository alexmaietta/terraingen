# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_test_suite(
    geometry
    SOURCES
    cells_test.cpp
    geometry_test.cpp
    plane_fit_test.cpp
    radial_shape_test.cpp
    surface_test.cpp
    LIBRARIES
    terraingen::geometry
    terraingen::random)
