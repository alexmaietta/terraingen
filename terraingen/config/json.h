/// @file
/// @brief Configurations as JSON: parsing, loading, writing, and the catalog and schema of the
/// format.
#pragma once

#include "terraingen/config/config.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace terraingen {

/// @brief A problem with a configuration, located by the JSON path of the offending value, such as
/// "layers[1].density.age_gyr" or "layers[0].stack[1].octaves".
class ConfigError : public std::runtime_error {
  public:
    /// @brief An error at a path; the message is "path: message", or the message alone at the
    /// root.
    /// @param path The JSON path of the value; empty for the document as a whole.
    /// @param message What is wrong.
    ConfigError(std::string path, const std::string& message);

    /// @brief The JSON path of the value.
    /// @return The path; empty for the document as a whole.
    const std::string& Path() const {
        return path_;
    }

  private:
    std::string path_;
};

/// @brief Reads a configuration from JSON text.
///
///     {"schema_version": 1,
///      "source": {"type": "flat", "width": 513, "height": 513, "pixel_size": 2},
///      "layers": [{"type": "noise", "stack": [{"wavelength": 500, "amplitude": 50}]},
///                 {"type": "craters", "density": {"type": "lunar_surface", "age_gyr": 3.5}}],
///      "filters": [{"type": "rim_smoothing", "sigma_radii": 0.05}],
///      "outputs": [{"type": "heightmap", "path": "terrain.tif"}]}
///
/// Keys are snake_case versions of the C++ fields; sources, layers, filters, outputs and models are
/// objects with a "type". Omitted keys take their defaults and omitted lists are empty.
/// @param json The JSON text.
/// @return The configuration.
/// @throws ConfigError for malformed JSON, a missing or unsupported "schema_version", an unknown
/// key, or a value of the wrong type.
TerrainConfig ParseConfig(std::string_view json);

/// @brief `ParseConfig` on the contents of a file.
/// @param path The file.
/// @return The configuration.
/// @throws ConfigError as `ParseConfig`, or when the file cannot be read.
TerrainConfig LoadConfig(const std::filesystem::path& path);

/// @brief A configuration as JSON with every value written out; parsing it gives back an equal
/// configuration.
/// @param config The configuration.
/// @param indent Spaces per indent level.
/// @return The JSON text.
std::string ConfigToJson(const TerrainConfig& config, int indent = 2);

/// @brief A reference of every configuration type with its defaults, as JSON.
///
/// The lists "surface", "source", "layers", "filters" and "outputs" hold each type of their kind,
/// "albedo" the albedo, and "models" one noise layer and every density, ages, degradation,
/// profile, boulder size, region shape, albedo source and albedo layer type. Each entry parses
/// where its kind belongs in a configuration.
/// @param indent Spaces per indent level.
/// @return The JSON text.
std::string ConfigCatalog(int indent = 2);

/// @brief The structure of a configuration as JSON, for editors: what each field holds and its
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
/// @param indent Spaces per indent level.
/// @return The JSON text.
std::string ConfigSchema(int indent = 2);

}  // namespace terraingen
