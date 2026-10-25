# SPDX-License-Identifier: BSD-3-Clause

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/third_party")

terraingen_add_library(
    pipeline
    SOURCES
    body_mesh.cpp
    feature_lists.cpp
    generator.cpp
    materials.cpp
    mesh_build.cpp
    report.cpp
    PUBLIC
    terraingen_distribution
    terraingen_layers
    terraingen_mesh
    terraingen_raster
    terraingen_sources
    terraingen_util
    PRIVATE
    $<BUILD_INTERFACE:nlohmann_json>)
