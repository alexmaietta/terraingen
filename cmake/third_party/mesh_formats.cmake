# SPDX-License-Identifier: BSD-3-Clause

# cgltf (glTF reading and writing) and tinyobjloader (OBJ and MTL reading), vendored single
# headers in third_party; their implementations compile into the mesh library.
add_library(terraingen_mesh_formats INTERFACE)
target_include_directories(
    terraingen_mesh_formats SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/third_party/cgltf"
                                             "${PROJECT_SOURCE_DIR}/third_party/tinyobjloader")
