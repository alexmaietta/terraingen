add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/third_party")

terraingen_add_library(
    pipeline
    SOURCES
    build.cpp
    config.cpp
    config_schema.cpp
    feature_io.cpp
    json_io.cpp
    run.cpp
    terrain.cpp
    tile_cache.cpp
    tiling.cpp
    validate.cpp
    PUBLIC
    terraingen_albedo
    terraingen_bodies
    terraingen_distribution
    terraingen_filters
    terraingen_layers
    terraingen_noise
    terraingen_raster
    terraingen_sources
    terraingen_util
    PRIVATE
    $<BUILD_INTERFACE:nlohmann_json>)
