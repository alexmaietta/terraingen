#pragma once

#include "terraingen/pipeline/config.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace terraingen {

/// A problem with a configuration, located by the JSON path of the offending value, such as
/// "craters.density.age_gyr" or "surface.layers[1].octaves".
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
/// Keys are snake_case versions of the C++ fields (`pixel_size`, `age_gyr`); models are objects
/// with a "type", such as {"type": "power_law", "coefficient": 0.1, "exponent": 2}. Omitted keys
/// take their defaults; unknown keys, wrong types and an unsupported `schema_version` are errors.
/// Omitting "craters" or setting it to null disables craters.
/// @throws ConfigError for malformed JSON or a value that does not fit the format.
TerrainConfig ParseConfig(std::string_view json);

/// `ParseConfig` on the contents of the file at `path`.
/// @throws ConfigError as `ParseConfig`, or when the file cannot be read.
TerrainConfig LoadConfig(const std::filesystem::path& path);

/// The configuration as JSON with every value written out, indented by `indent` spaces; parsing the
/// result gives back an equal configuration.
std::string ConfigToJson(const TerrainConfig& config, int indent = 2);

}  // namespace terraingen
