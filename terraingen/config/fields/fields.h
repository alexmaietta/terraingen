/// @file
/// @brief Every configuration field list, and the root configuration's.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/albedo.h"
#include "terraingen/config/fields/boulders.h"
#include "terraingen/config/fields/common.h"
#include "terraingen/config/fields/crater_models.h"
#include "terraingen/config/fields/craters.h"
#include "terraingen/config/fields/layers.h"
#include "terraingen/config/fields/outputs.h"
#include "terraingen/config/fields/sources.h"

namespace terraingen::config {

/// @brief The fields of the root configuration.
/// @param io The field reader, writer or describer.
/// @param config The configuration.
template <typename IO> void Fields(IO& io, TerrainConfig& config) {
    io("schema_version", config.schemaVersion,
       "Version of the configuration format, which this library reads; required.");
    io("surface", config.surface,
       "What the terrain lies on: a plane in metres, or a sphere (a planet or moon) in longitude "
       "and latitude degrees.");
    io("source", config.source,
       "The starting heights and the grid every layer, filter and output uses: flat, read from a "
       "GeoTIFF, or a chain of sources.");
    io("layers", config.layers,
       "Relief and features added to the source's heights, applied in order: noise, craters, "
       "boulders and more.");
    io("filters", config.filters, "Processing applied to the heights after every layer.");
    io("outputs", config.outputs,
       "Files written after generation: heightmaps, crater and boulder lists, boulder models, "
       "albedo and body meshes.");
    io("albedo", config.albedo,
       "The terrain's albedo: a source and the layers that change it, over the terrain's grid; "
       "none when unset.");
}

}  // namespace terraingen::config
