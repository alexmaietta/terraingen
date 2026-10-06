add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/third_party")

terraingen_add_library(
    pipeline
    SOURCES
    config_json.cpp
    run.cpp
    PUBLIC
    terraingen_core
    terraingen_distributions
    terraingen_features
    terraingen_io
    terraingen_noise
    terraingen_raster
    PRIVATE
    $<BUILD_INTERFACE:nlohmann_json>)
