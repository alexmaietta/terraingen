#include "terraingen/config/fields/json_io.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>

namespace terraingen {

namespace config {

void Fail(const std::string& path, const std::string& message) {
    throw ConfigError(path, message);
}

Reader::Reader(const Json& json, std::string path) : json_(json), path_(std::move(path)) {
    if (!json_.is_object()) {
        Fail(path_, std::string("expected an object, not ") + json_.type_name());
    }
}

void Reader::Finish() const {
    for (const auto& [key, value] : json_.items()) {
        if (known_.count(key) == 0) {
            std::string expected;
            for (const std::string& name : known_) {
                expected += (expected.empty() ? "" : ", ") + name;
            }
            Fail(Child(path_, key), "unknown key; expected one of: " + expected);
        }
    }
}

void Read(const Json& json, const std::string& path, double& value) {
    if (!json.is_number()) {
        Fail(path, std::string("expected a number, not ") + json.type_name());
    }
    value = json.get<double>();
    if (!std::isfinite(value)) {
        Fail(path, "expected a finite number");
    }
}

void Read(const Json& json, const std::string& path, float& value) {
    double number = 0.0;
    Read(json, path, number);
    if (std::fabs(number) > static_cast<double>(std::numeric_limits<float>::max())) {
        Fail(path, "number is too large");
    }
    value = static_cast<float>(number);
}

void Read(const Json& json, const std::string& path, bool& value) {
    if (!json.is_boolean()) {
        Fail(path, std::string("expected true or false, not ") + json.type_name());
    }
    value = json.get<bool>();
}

void Read(const Json& json, const std::string& path, std::string& value) {
    if (!json.is_string()) {
        Fail(path, std::string("expected a string, not ") + json.type_name());
    }
    value = json.get<std::string>();
}

void Read(const Json& json, const std::string& path, int& value) {
    if (!json.is_number_integer()) {
        Fail(path, std::string("expected an integer, not ") + json.type_name());
    }
    if (json.is_number_unsigned() &&
        json.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        Fail(path, "integer is too large");
    }
    const auto number = json.get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
        Fail(path, "integer is out of range");
    }
    value = static_cast<int>(number);
}

namespace {

/// Checks a configuration's "schema_version" before any other key, which a newer version may add.
/// @throws ConfigError at "schema_version" when the object `json` has none, or another version
/// than this library reads.
void CheckSchemaVersion(const Json& json) {
    if (!json.is_object()) {
        return;
    }
    const std::string supported =
        "this library reads version " + std::to_string(kConfigSchemaVersion);
    const auto found = json.find("schema_version");
    if (found == json.end()) {
        Fail("schema_version", "required; " + supported);
    }
    int version = 0;
    Read(*found, "schema_version", version);
    if (version != kConfigSchemaVersion) {
        Fail("schema_version",
             "version " + std::to_string(version) + " is not supported; " + supported);
    }
}

}  // namespace

}  // namespace config

ConfigError::ConfigError(std::string path, const std::string& message)
    : std::runtime_error(path.empty() ? message : path + ": " + message), path_(std::move(path)) {}

TerrainConfig ParseConfig(std::string_view json) {
    config::Json document;
    try {
        document = config::Json::parse(json);
    } catch (const config::Json::exception& error) {
        throw ConfigError("", std::string("invalid JSON: ") + error.what());
    }
    config::CheckSchemaVersion(document);
    TerrainConfig parsed;
    config::Read(document, "", parsed);
    return parsed;
}

TerrainConfig LoadConfig(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw ConfigError("", "cannot read " + path.string());
    }
    std::ostringstream text;
    text << file.rdbuf();
    return ParseConfig(text.str());
}

std::string ConfigToJson(const TerrainConfig& terrainConfig, int indent) {
    config::Json json = config::Json::object();
    TerrainConfig copy = terrainConfig;
    config::Writer writer(json, true);
    config::Fields(writer, copy);
    return json.dump(indent) + "\n";
}

ListFormat InferListFormat(std::string_view path) {
    constexpr std::string_view suffix = ".json";
    const bool json =
        path.size() >= suffix.size() && path.substr(path.size() - suffix.size()) == suffix;
    return json ? ListFormat::Json : ListFormat::Csv;
}

std::string LayerType(const LayerSpec& layer) {
    return std::visit(
        [](const auto& spec) {
            using Spec = std::decay_t<decltype(spec)>;
            std::string type(config::SpecName<Spec>::kName);
            if constexpr (std::is_same_v<Spec, RegionLayerSpec>) {
                type += " (" + LayerType(*spec.layer) + ")";
            }
            return type;
        },
        layer);
}

}  // namespace terraingen
