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

/// The fields of the root configuration.
template <typename IO> void Fields(IO& io, TerrainConfig& config) {
    io("schema_version", config.schemaVersion,
       "Version of the configuration format, which this library reads; required.");
    io("body", config.body,
       "What the terrain lies on: a plane in metres, or a whole body (a sphere, ellipsoid or "
       "shape model) in longitude and latitude degrees.");
    io("base", config.base,
       "The starting heights, read from a GeoTIFF or a chain of them, whose grid outputs cover "
       "unless they give a region; unset for the body's datum, 0 m, everywhere.");
    io("layers", config.layers,
       "Relief and features added to the base's heights, applied in order: noise, craters, "
       "boulders and more.");
    io("outputs", config.outputs,
       "Files written after generation, each over its own region or the whole body: heightmaps, "
       "crater and boulder lists, boulder models, albedo and meshes.");
    io("albedo", config.albedo,
       "The terrain's albedo: a source and the layers that change it, written by albedo outputs; "
       "none when unset.");
}

}  // namespace terraingen::config
