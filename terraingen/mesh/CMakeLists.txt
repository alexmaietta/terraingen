include("${PROJECT_SOURCE_DIR}/cmake/third_party/mesh_formats.cmake")

terraingen_add_library(
    mesh
    SOURCES
    io/gltf.cpp
    io/mesh_io.cpp
    io/obj.cpp
    io/stl.cpp
    mesh.cpp
    polyhedra.cpp
    PUBLIC
    terraingen_util
    PRIVATE
    $<BUILD_INTERFACE:terraingen_mesh_formats>)
