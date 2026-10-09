#pragma once

#include "terraingen/pipeline/config.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace terraingen {

/// A problem with a configuration, located by the JSON path of the offending value, such as
/// "layers[1].density.age_gyr" or "layers[0].stack[1].octaves".
class ConfigError : public std::runtime_error {
  public:
    ConfigError(std::string path, const std::string& message);

    /// JSON path of the value, empty for the document as a whole.
    const std::string& Path() const {
        return path_;
    }

  private:
    std::string path_;
};

/// Reads a configuration from JSON text.
///
/// A configuration has a "source", then "layers" and "filters" applied in order, and "outputs":
///
///     {"schema_version": 1,
///      "source": {"type": "flat", "width": 513, "height": 513, "pixel_size": 2},
///      "layers": [{"type": "noise", "stack": [{"wavelength": 500, "amplitude": 50}]},
///                 {"type": "craters", "density": {"type": "lunar_surface", "age_gyr": 3.5}}],
///      "filters": [{"type": "rim_smoothing", "sigma_radii": 0.05}],
///      "outputs": [{"type": "heightmap", "path": "terrain.tif"}]}
///
/// Keys are snake_case versions of the C++ fields (`pixel_size`, `age_gyr`); sources, layers,
/// filters, outputs and models are objects with a "type". Omitted keys take their defaults, and
/// omitted lists are empty; unknown keys, wrong types and an unsupported `schema_version` are
/// errors.
/// @throws ConfigError for malformed JSON or a value that does not fit the format.
TerrainConfig ParseConfig(std::string_view json);

/// `ParseConfig` on the contents of the file at `path`.
/// @throws ConfigError as `ParseConfig`, or when the file cannot be read.
TerrainConfig LoadConfig(const std::filesystem::path& path);

/// The configuration as JSON with every value written out, indented by `indent` spaces; parsing the
/// result gives back an equal configuration.
std::string ConfigToJson(const TerrainConfig& config, int indent = 2);

/// A reference of every configuration type with its defaults, as JSON: each surface, source,
/// layer, filter and output type (as the lists "surface", "source", "layers", "filters" and
/// "outputs"), the albedo, and under "models" one noise layer and every density, ages, degradation,
/// profile, rock size, region shape, albedo source and albedo layer type. Each entry is an object
/// that parses where its kind belongs in a configuration.
std::string ConfigCatalog(int indent = 2);

/// The structure of a configuration as JSON, for editors: what each field holds and its default.
///
/// The document has "root", the configuration object, and "definitions", every struct and choice
/// of types described once by id ("d0", "d1", ...) and referred to as {"kind": "ref", "ref": id},
/// since some contain themselves (a chain of sources). A schema has a "kind":
/// - "number", "integer" (with "minimum" 0 for counts; "fixed" for the schema version),
///   "boolean", "string";
/// - "enum" with its "choices" and, for each, its "descriptions";
/// - "optional" and "array" with the schema "of" what they hold and that thing's "default";
/// - "object" with its "fields", in order, each {"name", "description", "unit", "schema",
///   "default"}, "unit" the value's unit symbol (such as "m", "deg", "Gyr") or empty without one;
/// - "choice" with its "alternatives", each {"type", "description", "fields", "default"}: an
///   object whose "type" names which.
/// Every field, type and enum value has a description, a sentence or two for editors' tooltips.
/// It comes from the same field lists the parser reads, so it always matches it.
std::string ConfigSchema(int indent = 2);

}  // namespace terraingen
