include("${PROJECT_SOURCE_DIR}/cmake/third_party/mesh_formats.cmake")

terraingen_add_library(
    mesh
    SOURCES
    gltf_io.cpp
    mesh.cpp
    mesh_io.cpp
    obj_io.cpp
    polyhedra.cpp
    PUBLIC
    terraingen_util
    PRIVATE
    $<BUILD_INTERFACE:terraingen_mesh_formats>)
