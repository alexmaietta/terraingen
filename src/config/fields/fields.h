// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Every configuration field list, and the root configuration's.
#pragma once

#include "config/fields/albedo.h"
#include "config/fields/boulders.h"
#include "config/fields/common.h"
#include "config/fields/crater_models.h"
#include "config/fields/craters.h"
#include "config/fields/layers.h"
#include "config/fields/materials.h"
#include "config/fields/outputs.h"
#include "config/fields/sources.h"
#include "config/spec.h"

namespace terraingen::config {

/// The fields of the root configuration.
template <typename IO> void Fields(IO& io, ConfigSpec& config) {
    io("schema_version", config.schemaVersion,
       "Version of the configuration format, which this library reads; required.");
    io("body", config.body,
       "What the terrain lies on: a plane in meters, or a whole body (a sphere, ellipsoid or "
       "shape model) in longitude and latitude degrees.");
    io("source", config.source,
       "Where the heights come from: a GeoTIFF, a chain of sources, or synthetic noise, and how "
       "its posts are upsampled to finer spacings. A raster's grid is what outputs cover unless "
       "they give an extent and pixel size; unset is a synthetic source of rolling fBm noise, 400 "
       "m in wavelength and 12 m in amplitude, at the body's datum.");
    io("layers", config.layers,
       "Relief and features added to the source's heights, applied in order: noise, craters, "
       "boulders and more.");
    io("outputs", config.outputs,
       "What to produce, at what resolution and where, each over its own extent or the whole "
       "body: heightmaps, crater and boulder lists, boulder models, albedo, meshes and reports. "
       "Each is returned, and written to its path when it has one.");
    io("albedo", config.albedo,
       "The terrain's albedo: a source and the layers that change it, written by albedo outputs; "
       "none when unset.");
    io("materials", config.materials,
       "The material map: soil, sand, dust, bedrock and rock shares, and their Hapke parameters, "
       "written by materials and photometry outputs; none when unset.");
}

}  // namespace terraingen::config
