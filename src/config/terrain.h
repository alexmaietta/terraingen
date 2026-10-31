// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief A whole terrain configuration: its spec and fields, and the one pass that checks and
/// builds it.
#pragma once

#include "config/albedo.h"
#include "config/body.h"
#include "config/layers.h"
#include "config/materials.h"
#include "config/outputs.h"
#include "config/sources.h"

#include "terraingen/config/config.h"
#include "terraingen/config/validate.h"
#include "terraingen/pipeline/terrain.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// Everything a terrain configuration holds: the body it lies on, optionally its source of heights,
/// the layers added to them in order, the outputs to write, and optionally its albedo and
/// materials. `TerrainGenConfig` holds one as a value.
struct ConfigSpec {
    int schemaVersion = kConfigSchemaVersion;
    SurfaceSpec body{};
    /// None for the default synthetic source, `DefaultBaseNoise` at the body's datum.
    std::optional<SourceSpec> source{};
    std::vector<LayerSpec> layers;
    std::vector<OutputSpec> outputs;
    std::optional<AlbedoSpec> albedo;        ///< None by default.
    std::optional<MaterialsSpec> materials;  ///< None by default.
    /// The folder the configuration was read from (`LoadConfig`), which its relative paths
    /// resolve against when a call gives no folder; not part of its JSON.
    std::filesystem::path directory{};
};

template <typename IO> void Fields(IO& io, ConfigSpec& config) {
    io("schema_version", config.schemaVersion,
       "Version of the configuration format, which this library reads.", "", Required());
    io("body", config.body,
       "What the terrain lies on: a plane in meters, or a whole body (a sphere, ellipsoid or "
       "shape model) in longitude and latitude degrees.");
    io("source", config.source,
       "Where the heights come from: a GeoTIFF, a chain of sources or synthetic noise, with its "
       "upsampling; unset is fBm noise 400 m in wavelength and 12 m in amplitude at the datum.");
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

/// The folder relative paths of `config` resolve against: `given`, or without one the folder
/// `config` was read from.
inline std::filesystem::path ConfigDirectory(const ConfigSpec& config,
                                             const std::filesystem::path& given) {
    return given.empty() ? config.directory : given;
}

namespace config {

/// A configuration's terrain and summary, from one pass that checks each part as it builds it.
struct CheckedTerrain {
    Terrain terrain;
    ConfigSummary summary;
    std::vector<OutputGrid> outputs;  ///< What each output covers, in order.
};

/// Checks and builds `config` in one pass, on `external` in place of its source when given: every
/// field range, the body, the source, the outputs, the layers, the albedo and the materials.
/// @throws ConfigError naming the first invalid value by its JSON path.
CheckedTerrain BuildCheckedTerrain(const ConfigSpec& config, const std::filesystem::path& directory,
                                   const std::shared_ptr<const Source>& external = nullptr);

}  // namespace config

}  // namespace terraingen
