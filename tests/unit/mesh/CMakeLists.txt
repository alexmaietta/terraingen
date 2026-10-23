# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_test_suite(
    mesh
    SOURCES
    io/icq_test.cpp
    io/mesh_io_test.cpp
    mesh_test.cpp
    polyhedra_test.cpp
    shape_model_test.cpp
    LIBRARIES
    terraingen::mesh)
