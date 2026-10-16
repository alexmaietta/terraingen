terraingen_add_test_suite(
    mesh
    SOURCES
    io/mesh_io_test.cpp
    mesh_test.cpp
    polyhedra_test.cpp
    LIBRARIES
    terraingen::mesh)
