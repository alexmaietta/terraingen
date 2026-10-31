# SPDX-License-Identifier: BSD-3-Clause

include("${PROJECT_SOURCE_DIR}/cmake/third_party/mesh_formats.cmake")

terraingen_add_library(
    mesh
    SOURCES
    io/gltf.cpp
    io/icq.cpp
    io/mesh_io.cpp
    io/obj.cpp
    io/stl.cpp
    mesh.cpp
    mesh_build.cpp
    polyhedra.cpp
    shape_model.cpp
    PUBLIC
    terraingen_geometry
    terraingen_raster
    terraingen_util
    PRIVATE
    $<BUILD_INTERFACE:terraingen_mesh_formats>)
