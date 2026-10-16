/// @file
/// @brief Configurations as JSON: parsing, loading, writing, catalog and schema.
#pragma once

#include "terraingen/config/config.h"

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
///      "source": {"type": "flat", "width": 513, "height": 513, "pixel_size": 2},
///      "layers": [{"type": "noise", "stack": [{"wavelength": 500, "amplitude": 50}]},
///                 {"type": "craters",
///                  "density": {"type": "production_surface", "age_gyr": 3.5}}],
///      "filters": [{"type": "rim_smoothing", "sigma_radii": 0.05}],
///      "outputs": [{"type": "heightmap", "path": "terrain.tif"}]}
///
/// Keys are snake_case versions of the C++ fields; sources, layers, filters, outputs and models are
/// objects with a "type". Omitted keys take their defaults and omitted lists are empty.
/// @throws ConfigError for malformed JSON, a missing or unsupported "schema_version", an unknown
/// key, or a value of the wrong type.
TerrainConfig ParseConfig(std::string_view json);

/// `ParseConfig` on the contents of a file.
/// @throws ConfigError as `ParseConfig`, or when the file cannot be read.
TerrainConfig LoadConfig(const std::filesystem::path& path);

/// A configuration as JSON with every value written out; parsing it gives back an equal
/// configuration.
std::string ConfigToJson(const TerrainConfig& config, int indent = 2);

/// A reference of every configuration type with its defaults, as JSON.
///
/// The lists "surface", "source", "layers", "filters" and "outputs" hold each type of their kind,
/// "albedo" the albedo, and "models" one noise layer and every density, ages, degradation,
/// profile, boulder size, region shape, albedo source and albedo layer type. Each entry parses
/// where its kind belongs in a configuration.
std::string ConfigCatalog(int indent = 2);

/// The structure of a configuration as JSON, for editors: what each field holds and its
/// default, from the same field lists the parser reads.
///
/// The document has "schema_version", "root" (the configuration object) and "definitions", each
/// struct and choice described once by id ("d0", "d1", ...) and referred to as
/// {"kind": "ref", "ref": id}. A schema has a "kind":
/// - "number", "integer" ("minimum" 0 for counts, "fixed" for the schema version), "boolean",
///   "string";
/// - "enum" with its "choices" and their "descriptions";
/// - "optional" and "array" with the schema "of" what they hold and that thing's "default";
/// - "object" with its "fields", each {"name", "description", "unit", "schema", "default"};
/// - "choice" with its "alternatives", each {"type", "description", "fields", "default"}.
std::string ConfigSchema(int indent = 2);

}  // namespace terraingen
