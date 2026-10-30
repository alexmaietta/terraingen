# SPDX-License-Identifier: BSD-3-Clause

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/third_party")

terraingen_add_library(
    io
    SOURCES
    feature_lists.cpp
    report_json.cpp
    PUBLIC
    terraingen_geometry
    terraingen_layers
    terraingen_mesh
    terraingen_pipeline
    terraingen_raster
    terraingen_util
    PRIVATE
    $<BUILD_INTERFACE:nlohmann_json>)
