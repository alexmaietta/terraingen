add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/third_party")

terraingen_add_library(
    config
    SOURCES
    build.cpp
    config.cpp
    feature_lists.cpp
    generate.cpp
    json.cpp
    schema.cpp
    validate.cpp
    PUBLIC
    terraingen_albedo
    terraingen_distribution
    terraingen_layers
    terraingen_noise
    terraingen_materials
    terraingen_pipeline
    terraingen_raster
    terraingen_sources
    terraingen_util
    PRIVATE
    $<BUILD_INTERFACE:nlohmann_json>)

# MSVC's optimizer crashes (C1001) on the deep template instantiations of the JSON reading and
# schema, which are not hot, so they build without optimization there.
if(MSVC)
    set_source_files_properties(json.cpp schema.cpp PROPERTIES COMPILE_OPTIONS /Od)
endif()
