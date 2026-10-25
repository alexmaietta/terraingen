# SPDX-License-Identifier: BSD-3-Clause

find_package(Threads REQUIRED)
terraingen_add_library(
    util
    SOURCES
    checks.cpp
    cube_sphere_cells.cpp
    fft.cpp
    files.cpp
    surface.cpp
    log.cpp
    parallel.cpp
    planar_cells.cpp
    plane_fit.cpp
    radial_shape.cpp
    special_functions.cpp
    text.cpp
    version.cpp
    PUBLIC
    Threads::Threads)
