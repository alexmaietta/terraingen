/// @file
/// @brief Reading configuration values from JSON and writing them back through the field lists.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/checks.h"
#include "terraingen/config/fields/enums.h"
#include "terraingen/config/fields/fields.h"
#include "terraingen/config/json.h"

#include <nlohmann/json.hpp>

#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen::config {

/// The JSON type configurations are read from and written to, keeping keys in order.
using Json = nlohmann::ordered_json;

/// Reports a problem with the configuration.
/// @throws ConfigError at `path` with `message`, always.
[[noreturn]] void Fail(const std::string& path, const std::string& message);

/// Reads the keys of one JSON object into fields, remembering which keys were asked for so
/// that any other key can be reported.
class Reader {
  public:
    /// A reader of one object at `path`.
    /// @param json The object; it must outlive the reader.
    /// @throws ConfigError at `path` if `json` is not an object.
    Reader(const Json& json, std::string path);

    /// Reads a key into a field when present; `description` and `unit` are for `ConfigSchema`.
    /// @throws ConfigError at the key's path for a value that does not fit.
    template <typename T>
    void operator()(std::string_view key, T& value, std::string_view description = {},
                    std::string_view unit = {});

    /// Marks a key as known without reading it.
    void Accept(std::string_view key) {
        known_.emplace(key);
    }

    /// Checks that the object has no key that no field asked for.
    /// @throws ConfigError at the first unknown key.
    void Finish() const;

  private:
    const Json& json_;
    std::string path_;
    std::set<std::string, std::less<>> known_;
};

/// Writes fields as the keys of a JSON object, in order.
class Writer {
  public:
    /// A writer into one object.
    /// @param json The object to write; it must outlive the writer.
    explicit Writer(Json& json) : json_(json) {}

    /// Writes a field as a key; `description` and `unit` are for `ConfigSchema`.
    template <typename T>
    void operator()(std::string_view key, T& value, std::string_view description = {},
                    std::string_view unit = {});

  private:
    Json& json_;
};

/// A struct read and written as a JSON object through its `Fields`.
template <typename T>
concept HasFields = requires(Reader& reader, T& value) { Fields(reader, value); };

/// @name Read
/// Stores a JSON value at `path` in a configuration value of the overload's type.
/// @throws ConfigError at `path`, or below it, for a value that does not fit.
/// @{
void Read(const Json& json, const std::string& path, double& value);
void Read(const Json& json, const std::string& path, float& value);
void Read(const Json& json, const std::string& path, bool& value);
void Read(const Json& json, const std::string& path, std::string& value);
void Read(const Json& json, const std::string& path, int& value);
template <std::unsigned_integral T>
    requires(!std::same_as<T, bool>)
void Read(const Json& json, const std::string& path, T& value);
template <NamedEnum E> void Read(const Json& json, const std::string& path, E& value);
template <HasFields T> void Read(const Json& json, const std::string& path, T& value);
template <typename T> void Read(const Json& json, const std::string& path, std::optional<T>& value);
template <typename T> void Read(const Json& json, const std::string& path, Boxed<T>& value);
template <typename T> void Read(const Json& json, const std::string& path, std::vector<T>& value);
template <typename... Ts>
void Read(const Json& json, const std::string& path, std::variant<Ts...>& value);
/// @}

/// @name AfterRead
/// Finishes a value after its object is read: nothing, but list outputs take their format
/// from the path's extension when the JSON gives none.
/// @{
template <typename T> void AfterRead(const Json& /*json*/, T& /*value*/) {}
void AfterRead(const Json& json, CraterListOutput& output);
void AfterRead(const Json& json, BoulderListOutput& output);
/// @}

/// Reads an object at `path` into a value through its `Fields`.
/// @param typed Whether the value is a variant alternative, so a "type" key is expected too.
/// @throws ConfigError at `path`, or below it, for an object that does not fit.
template <typename T>
void ReadObject(const Json& json, const std::string& path, T& value, bool typed = false) {
    Reader reader(json, path);
    if (typed) {
        reader.Accept("type");
    }
    Fields(reader, value);
    reader.Finish();
    AfterRead(json, value);
}

template <std::unsigned_integral T>
    requires(!std::same_as<T, bool>)
void Read(const Json& json, const std::string& path, T& value) {
    if (!json.is_number_integer()) {
        Fail(path, std::string("expected a non-negative integer, not ") + json.type_name());
    }
    if (!json.is_number_unsigned()) {
        Fail(path, "expected a non-negative integer");
    }
    const auto number = json.get<std::uint64_t>();
    if (number > std::numeric_limits<T>::max()) {
        Fail(path, "integer is too large");
    }
    value = static_cast<T>(number);
}

template <NamedEnum E> void Read(const Json& json, const std::string& path, E& value) {
    std::string name;
    Read(json, path, name);
    std::string expected;
    for (const auto& entry : EnumNames<E>::kValues) {
        if (entry.name == name) {
            value = entry.value;
            return;
        }
        expected += (expected.empty() ? "" : ", ") + std::string(entry.name);
    }
    Fail(path, "unknown value \"" + name + "\"; expected one of: " + expected);
}

template <HasFields T> void Read(const Json& json, const std::string& path, T& value) {
    ReadObject(json, path, value);
}

template <typename T>
void Read(const Json& json, const std::string& path, std::optional<T>& value) {
    if (json.is_null()) {
        value.reset();
        return;
    }
    T inner = value.value_or(T{});
    Read(json, path, inner);
    value = std::move(inner);
}

template <typename T> void Read(const Json& json, const std::string& path, Boxed<T>& value) {
    Read(json, path, *value);
}

template <typename T> void Read(const Json& json, const std::string& path, std::vector<T>& value) {
    if (!json.is_array()) {
        Fail(path, std::string("expected an array, not ") + json.type_name());
    }
    std::vector<T> items(json.size());
    for (std::size_t i = 0; i < json.size(); ++i) {
        Read(json[i], Element(path, i), items[i]);
    }
    value = std::move(items);
}

/// Reads an object into a variant as the alternative `T` if its "type" `name` is `T`'s, and
/// returns whether it was.
/// @throws ConfigError at `path`, or below it, for an object that does not fit `T`.
template <typename T, typename Variant>
bool ReadAlternative(const Json& json, const std::string& path, const std::string& name,
                     Variant& value) {
    if (name != SpecName<T>::kName) {
        return false;
    }
    T spec{};
    ReadObject(json, path, spec, true);
    value = std::move(spec);
    return true;
}

template <typename... Ts>
void Read(const Json& json, const std::string& path, std::variant<Ts...>& value) {
    if (!json.is_object()) {
        Fail(path, std::string("expected an object with a \"type\", not ") + json.type_name());
    }
    std::string expected;
    ((expected += (expected.empty() ? "" : ", ") + std::string(SpecName<Ts>::kName)), ...);
    const auto type = json.find("type");
    if (type == json.end() || !type->is_string()) {
        Fail(Child(path, "type"), "expected a string, one of: " + expected);
    }
    const std::string name = type->template get<std::string>();
    if (!(ReadAlternative<Ts>(json, path, name, value) || ...)) {
        Fail(Child(path, "type"), "unknown type \"" + name + "\"; expected one of: " + expected);
    }
}

template <typename T>
void Reader::operator()(std::string_view key, T& value, std::string_view /*description*/,
                        std::string_view /*unit*/) {
    known_.emplace(key);
    const auto found = json_.find(key);
    if (found != json_.end()) {
        Read(*found, Child(path_, key), value);
    }
}

/// @name ToJson
/// A configuration value as JSON, as `Read` reads it.
/// @{
inline Json ToJson(double value) {
    return value;
}
inline Json ToJson(float value) {
    return static_cast<double>(value);
}
inline Json ToJson(bool value) {
    return value;
}
inline Json ToJson(int value) {
    return value;
}
inline Json ToJson(const std::string& value) {
    return value;
}
template <std::unsigned_integral T>
    requires(!std::same_as<T, bool>)
Json ToJson(T value) {
    return static_cast<std::uint64_t>(value);
}
template <NamedEnum E> Json ToJson(E value);
template <HasFields T> Json ToJson(const T& value);
template <typename T> Json ToJson(const std::optional<T>& value);
template <typename T> Json ToJson(const Boxed<T>& value);
template <typename T> Json ToJson(const std::vector<T>& value);
template <typename... Ts> Json ToJson(const std::variant<Ts...>& value);
/// @}

template <NamedEnum E> Json ToJson(E value) {
    for (const auto& entry : EnumNames<E>::kValues) {
        if (entry.value == value) {
            return std::string(entry.name);
        }
    }
    return nullptr;
}

template <HasFields T> Json ToJson(const T& value) {
    Json json = Json::object();
    T copy = value;
    Writer writer(json);
    Fields(writer, copy);
    return json;
}

template <typename T> Json ToJson(const std::optional<T>& value) {
    return value ? ToJson(*value) : Json(nullptr);
}

template <typename T> Json ToJson(const Boxed<T>& value) {
    return ToJson(*value);
}

template <typename T> Json ToJson(const std::vector<T>& value) {
    Json json = Json::array();
    for (const T& item : value) {
        json.push_back(ToJson(item));
    }
    return json;
}

template <typename... Ts> Json ToJson(const std::variant<Ts...>& value) {
    return std::visit(
        [](const auto& spec) {
            using Spec = std::decay_t<decltype(spec)>;
            Json json = Json::object();
            json["type"] = std::string(SpecName<Spec>::kName);
            const Json fields = ToJson(spec);
            for (const auto& item : fields.items()) {
                json[item.key()] = item.value();
            }
            return json;
        },
        value);
}

template <typename T>
void Writer::operator()(std::string_view key, T& value, std::string_view /*description*/,
                        std::string_view /*unit*/) {
    json_[std::string(key)] = ToJson(value);
}

}  // namespace terraingen::config
