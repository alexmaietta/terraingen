// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Configurations as JSON: parsing, loading, writing, catalog and schema.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/pipeline/feature_lists.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace terraingen {

/// A problem with a configuration, located by the JSON path of the offending value, such as
/// "layers[1].density.age_gyr" or "layers[0].stack[1].octaves".
class ConfigError : public std::runtime_error {
  public:
    /// An error at a path; the message is "path: message", or the message alone at the root.
    /// @param path The JSON path of the value; empty for the document as a whole.
    ConfigError(std::string path, const std::string& message);

    /// The JSON path of the value; empty for the document as a whole.
    const std::string& Path() const {
        return path_;
    }

  private:
    std::string path_;
};

/// Reads a configuration from JSON text.
///
///     {"schema_version": 1,
///      "layers": [{"type": "noise", "stack": [{"wavelength_m": 500, "amplitude_m": 50}]},
///                 {"type": "craters",
///                  "density": {"type": "production_surface", "age_gyr": 3.5},
///                  "apply": {"rim_smoothing": {"sigma_radii": 0.05}}}],
///      "outputs": [{"type": "heightmap", "path": "terrain.tif",
///                   "pixel_size_m": 2,
///                   "extent_m": {"center": [0, 0], "size": [1026, 1026]}}]}
///
/// Keys are snake_case versions of the C++ fields; bodies, sources, layers, outputs and models are
/// objects with a "type". Omitted keys take their defaults and omitted lists are empty.
/// @throws ConfigError for malformed JSON, a missing or unsupported "schema_version", an unknown
/// key, or a value of the wrong type.
TerrainGenConfig ParseConfig(std::string_view json);

/// `ParseConfig` on the contents of a file, remembering its folder in `directory`.
/// @throws ConfigError as `ParseConfig`, or when the file cannot be read.
TerrainGenConfig LoadConfig(const std::filesystem::path& path);

/// A configuration as JSON with every value written out but an unset source and albedo; parsing it
/// gives back an equal configuration.
std::string ConfigToJson(const TerrainGenConfig& config, int indent = 2);

/// Every configuration type with its defaults, as JSON: lists "body", "source", "layers",
/// "outputs" and "models" whose entries each parse where their kind belongs, and the "albedo".
std::string ConfigCatalog(int indent = 2);

/// The structure of a configuration as JSON, for editors, from the field lists the parser reads:
/// "root" and "definitions" of schemas, each with a "kind" (number, integer, boolean, string, enum,
/// optional, array, object, choice or ref) and, for fields, their description, unit, default and
/// bounds.
std::string ConfigSchema(int indent = 2);

/// The format a list file's path implies: JSON for a .json path, else CSV.
ListFormat InferListFormat(std::string_view path);

}  // namespace terraingen
