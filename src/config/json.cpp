// SPDX-License-Identifier: BSD-3-Clause

#include "config/json_io.h"

#include "terraingen/sources/upsampling.h"
#include "util/text.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

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
    if (!missing_.empty()) {
        Fail(Child(path_, missing_.front()), "required");
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

void Read(const Json& json, const std::string& path, std::array<double, 2>& value) {
    if (!json.is_array() || json.size() != 2) {
        Fail(path, std::string("expected a list of two numbers, not ") +
                       (json.is_array() ? "a list of " + std::to_string(json.size())
                                        : std::string(json.type_name())));
    }
    std::array<double, 2> pair{};
    for (std::size_t i = 0; i < 2; ++i) {
        Read(json[i], Element(path, i), pair[i]);
    }
    value = pair;
}

void Read(const Json& json, const std::string& path, UpsamplingSpec& value) {
    UpsamplingSpec spec;
    if (json.is_string()) {
        spec.type = json.get<std::string>();
        if (spec.type.empty()) {
            Fail(path, "must not be empty");
        }
        value = std::move(spec);
        return;
    }
    if (!json.is_object()) {
        Fail(path, std::string("expected an upsampling name or an object with a \"type\", not ") +
                       json.type_name());
    }
    const auto type = json.find("type");
    if (type == json.end() || !type->is_string() || type->get<std::string>().empty()) {
        Fail(Child(path, "type"), "expected an upsampling name");
    }
    spec.type = type->get<std::string>();
    if (KernelUpsamplerMethod(spec.type)) {
        Reader reader(json, path);
        reader.Accept("type");
        reader.Finish();
    } else if (spec.type == "diamond_square") {
        ReadObject(json, path, spec.diamondSquare, true);
    } else {
        Json settings = json;
        settings.erase("type");
        if (!settings.empty()) {
            spec.settings = settings.dump();
        }
    }
    value = std::move(spec);
}

Json TypedJson(std::string_view type, const Json& fields) {
    Json json = Json::object();
    json["type"] = std::string(type);
    for (const auto& item : fields.items()) {
        json[item.key()] = item.value();
    }
    return json;
}

Json ToJson(const UpsamplingSpec& value, bool compact) {
    if (value.type == "diamond_square") {
        return TypedJson(value.type, FieldsJson(value.diamondSquare, false, compact));
    }
    if (value.settings.empty()) {
        return value.type;
    }
    return TypedJson(value.type, Json::parse(value.settings));
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

TerrainGenConfig ParseConfig(std::string_view json) {
    config::Json document;
    try {
        document = config::Json::parse(json);
    } catch (const config::Json::exception& error) {
        throw ConfigError("", std::string("invalid JSON: ") + error.what());
    }
    config::CheckSchemaVersion(document);
    ConfigSpec parsed;
    config::Read(document, "", parsed);
    return parsed;
}

TerrainGenConfig LoadConfig(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw ConfigError("", "cannot read " + path.string());
    }
    std::ostringstream text;
    text << file.rdbuf();
    ConfigSpec config = ParseConfig(text.str());
    config.directory = path.parent_path();
    return config;
}

std::string ConfigToJson(const TerrainGenConfig& terrainConfig, int indent) {
    return config::FieldsJson(terrainConfig.Spec(), true, true).dump(indent) + "\n";
}

bool operator==(const TerrainGenConfig& a, const TerrainGenConfig& b) {
    return config::FieldsJson(a.Spec()) == config::FieldsJson(b.Spec());
}

bool operator!=(const TerrainGenConfig& a, const TerrainGenConfig& b) {
    return !(a == b);
}

ListFormat InferListFormat(std::string_view path) {
    return HasExtension(std::filesystem::path(path), {".json"}) ? ListFormat::Json
                                                                : ListFormat::Csv;
}

}  // namespace terraingen
