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

}  // namespace terraingen
