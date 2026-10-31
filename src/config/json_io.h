// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Reading configuration values from JSON and writing them back through the field lists.
#pragma once

#include "config/terrain.h"
#include "terraingen/config/json.h"

#include <nlohmann/json.hpp>

#include <array>
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

/// Reads the keys of one JSON object into fields, remembering which keys were asked for so
/// that any other key can be reported.
class Reader {
  public:
    /// A reader of one object at `path`.
    /// @param json The object; it must outlive the reader.
    /// @throws ConfigError at `path` if `json` is not an object.
    Reader(const Json& json, std::string path);

    /// Reads a key into a field when present, else remembers it for `Finish` when `range`
    /// requires it; `description` and `unit` are for `ConfigSchema`.
    /// @throws ConfigError at the key's path for a value that does not fit.
    template <typename T>
    void operator()(std::string_view key, T& value, std::string_view description = {},
                    std::string_view unit = {}, const FieldRange& range = {});

    /// Marks a key as known without reading it.
    void Accept(std::string_view key) {
        known_.emplace(key);
    }

    /// Checks that the object has no key that no field asked for, then that it has every key a
    /// field requires.
    /// @throws ConfigError at the first unknown key, else "required" at the first missing one.
    void Finish() const;

  private:
    const Json& json_;
    std::string path_;
    std::set<std::string, std::less<>> known_;
    std::vector<std::string> missing_;
};

/// Writes fields as the keys of a JSON object, in order.
class Writer {
  public:
    /// A writer into one object.
    /// @param json The object to write; it must outlive the writer.
    /// @param omitUnset Whether unset optional fields of this object are left out rather than
    /// written as null; the objects inside it write theirs as null.
    /// @param compact Whether the objects inside it leave out lists that are empty, as they are
    /// by default (`FieldsJson`).
    explicit Writer(Json& json, bool omitUnset = false, bool compact = false)
        : json_(json), omitUnset_(omitUnset), compact_(compact) {}

    /// Writes a field as a key; `description` and `unit` are for `ConfigSchema`.
    template <typename T>
    void operator()(std::string_view key, T& value, std::string_view description = {},
                    std::string_view unit = {}, const FieldRange& range = {});

  private:
    Json& json_;
    bool omitUnset_ = false;
    bool compact_ = false;
};

/// Whether `Fields(reader, value)` is callable with a `Reader`.
template <typename T, typename = void> struct HasFieldsTrait : std::false_type {};
template <typename T>
struct HasFieldsTrait<T, std::void_t<decltype(Fields(std::declval<Reader&>(), std::declval<T&>()))>>
    : std::true_type {};

/// A struct read and written as a JSON object through its `Fields`.
template <typename T> inline constexpr bool HasFields = HasFieldsTrait<T>::value;

/// An unsigned integer type other than bool, read and written as a non-negative integer.
template <typename T>
inline constexpr bool IsUnsignedInteger =
    std::is_integral_v<T> && std::is_unsigned_v<T> && !std::is_same_v<T, bool>;

/// @name Read
/// Stores a JSON value at `path` in a configuration value of the overload's type.
/// @throws ConfigError at `path`, or below it, for a value that does not fit.
/// @{
void Read(const Json& json, const std::string& path, double& value);
void Read(const Json& json, const std::string& path, float& value);
void Read(const Json& json, const std::string& path, bool& value);
void Read(const Json& json, const std::string& path, std::string& value);
void Read(const Json& json, const std::string& path, int& value);
/// A pair of numbers, such as a fade's start and end sample counts, from a list of exactly two.
void Read(const Json& json, const std::string& path, std::array<double, 2>& value);
/// An upsampling, from a name alone or an object with a "type": a kernel's object has no other
/// key, diamond_square's has its fields, and another type's keys are kept as its settings.
void Read(const Json& json, const std::string& path, UpsamplingSpec& value);
template <typename T, std::enable_if_t<IsUnsignedInteger<T>, int> = 0>
void Read(const Json& json, const std::string& path, T& value);
template <typename E, std::enable_if_t<NamedEnum<E>, int> = 0>
void Read(const Json& json, const std::string& path, E& value);
template <typename T, std::enable_if_t<HasFields<T>, int> = 0>
void Read(const Json& json, const std::string& path, T& value);
template <typename T> void Read(const Json& json, const std::string& path, std::optional<T>& value);
template <typename T> void Read(const Json& json, const std::string& path, Boxed<T>& value);
template <typename T> void Read(const Json& json, const std::string& path, std::vector<T>& value);
template <typename... Ts>
void Read(const Json& json, const std::string& path, std::variant<Ts...>& value);
/// @}

/// Reads an object at `path` into a value through its `Fields`; a list output given no "format"
/// takes it from its path (`InferListFormat`).
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
    if constexpr (std::is_base_of_v<ListOutput, T>) {
        if (!json.contains("format")) {
            value.format = InferListFormat(value.path);
        }
    }
}

template <typename T, std::enable_if_t<IsUnsignedInteger<T>, int>>
void Read(const Json& json, const std::string& path, T& value) {
    if (!json.is_number_integer()) {
        throw ConfigError(path,
                          std::string("expected a non-negative integer, not ") + json.type_name());
    }
    if (!json.is_number_unsigned()) {
        throw ConfigError(path, "expected a non-negative integer");
    }
    const auto number = json.get<std::uint64_t>();
    if (number > std::numeric_limits<T>::max()) {
        throw ConfigError(path, "integer is too large");
    }
    value = static_cast<T>(number);
}

template <typename E, std::enable_if_t<NamedEnum<E>, int>>
void Read(const Json& json, const std::string& path, E& value) {
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
    throw ConfigError(path, "unknown value \"" + name + "\"; expected one of: " + expected);
}

template <typename T, std::enable_if_t<HasFields<T>, int>>
void Read(const Json& json, const std::string& path, T& value) {
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
        throw ConfigError(path, std::string("expected an array, not ") + json.type_name());
    }
    std::vector<T> items(json.size());
    for (std::size_t i = 0; i < json.size(); ++i) {
        Read(json[i], ItemPath(path, i), items[i]);
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
        throw ConfigError(path, std::string("expected an object with a \"type\", not ") +
                                    json.type_name());
    }
    std::string expected;
    ((expected += (expected.empty() ? "" : ", ") + std::string(SpecName<Ts>::kName)), ...);
    const auto type = json.find("type");
    if (type == json.end() || !type->is_string()) {
        throw ConfigError(FieldPath(path, "type"), "expected a string, one of: " + expected);
    }
    const std::string name = type->template get<std::string>();
    if (!(ReadAlternative<Ts>(json, path, name, value) || ...)) {
        throw ConfigError(FieldPath(path, "type"),
                          "unknown type \"" + name + "\"; expected one of: " + expected);
    }
}

template <typename T>
void Reader::operator()(std::string_view key, T& value, std::string_view /*description*/,
                        std::string_view /*unit*/, const FieldRange& range) {
    known_.emplace(key);
    const auto found = json_.find(key);
    if (found != json_.end()) {
        Read(*found, FieldPath(path_, key), value);
    } else if (range.required) {
        missing_.emplace_back(key);
    }
}

/// @name ToJson
/// A configuration value as JSON, as `Read` reads it.
/// @param compact Whether objects leave out lists that are empty, as they are by default
/// (`FieldsJson`), rather than write every field.
/// @{
inline Json ToJson(double value, bool /*compact*/ = false) {
    return value;
}
inline Json ToJson(float value, bool /*compact*/ = false) {
    return static_cast<double>(value);
}
inline Json ToJson(bool value, bool /*compact*/ = false) {
    return value;
}
inline Json ToJson(int value, bool /*compact*/ = false) {
    return value;
}
inline Json ToJson(const std::string& value, bool /*compact*/ = false) {
    return value;
}
inline Json ToJson(const std::array<double, 2>& value, bool /*compact*/ = false) {
    return Json::array({value[0], value[1]});
}
template <typename T, std::enable_if_t<IsUnsignedInteger<T>, int> = 0>
Json ToJson(T value, bool /*compact*/ = false) {
    return static_cast<std::uint64_t>(value);
}
/// An upsampling as its name alone when it has no settings, else as an object with its "type".
Json ToJson(const UpsamplingSpec& value, bool compact = false);
template <typename E, std::enable_if_t<NamedEnum<E>, int> = 0>
Json ToJson(E value, bool compact = false);
template <typename T, std::enable_if_t<HasFields<T>, int> = 0>
Json ToJson(const T& value, bool compact = false);
template <typename T> Json ToJson(const std::optional<T>& value, bool compact = false);
template <typename T> Json ToJson(const Boxed<T>& value, bool compact = false);
template <typename T> Json ToJson(const std::vector<T>& value, bool compact = false);
template <typename... Ts> Json ToJson(const std::variant<Ts...>& value, bool compact = false);
/// @}

template <typename E, std::enable_if_t<NamedEnum<E>, int>> Json ToJson(E value, bool /*compact*/) {
    for (const auto& entry : EnumNames<E>::kValues) {
        if (entry.value == value) {
            return std::string(entry.name);
        }
    }
    return nullptr;
}

/// The keys of `T`'s fields that hold an empty list by default.
template <typename T> const std::set<std::string, std::less<>>& EmptyListKeys() {
    static const std::set<std::string, std::less<>> keys = [] {
        std::set<std::string, std::less<>> found;
        const Json defaults = ToJson(T{});
        for (const auto& item : defaults.items()) {
            if (item.value().is_array() && item.value().empty()) {
                found.insert(item.key());
            }
        }
        return found;
    }();
    return keys;
}

/// The fields of `value` as an object.
/// @param omitUnset Whether unset optional fields of this object are left out (`Writer`).
/// @param compact Whether lists that are empty, as they are by default, are left out here and in
/// the objects inside; `Read` reads them back the same.
template <typename T>
Json FieldsJson(const T& value, bool omitUnset = false, bool compact = false) {
    Json json = Json::object();
    T copy = value;
    Writer writer(json, omitUnset, compact);
    Fields(writer, copy);
    if (compact) {
        for (const std::string& key : EmptyListKeys<T>()) {
            const auto found = json.find(key);
            if (found != json.end() && found->is_array() && found->empty()) {
                json.erase(found);
            }
        }
    }
    return json;
}

template <typename T, std::enable_if_t<HasFields<T>, int>>
Json ToJson(const T& value, bool compact) {
    return FieldsJson(value, false, compact);
}

template <typename T> Json ToJson(const std::optional<T>& value, bool compact) {
    return value ? ToJson(*value, compact) : Json(nullptr);
}

template <typename T> Json ToJson(const Boxed<T>& value, bool compact) {
    return ToJson(*value, compact);
}

template <typename T> Json ToJson(const std::vector<T>& value, bool compact) {
    Json json = Json::array();
    for (const T& item : value) {
        json.push_back(ToJson(item, compact));
    }
    return json;
}

/// An object with the "type" `type` followed by the keys of the object `fields`.
Json TypedJson(std::string_view type, const Json& fields);

template <typename... Ts> Json ToJson(const std::variant<Ts...>& value, bool compact) {
    return std::visit(
        [compact](const auto& spec) {
            using Spec = std::decay_t<decltype(spec)>;
            return TypedJson(SpecName<Spec>::kName, ToJson(spec, compact));
        },
        value);
}

/// @name IsUnset
/// Whether a field is an unset optional.
/// @{
template <typename T> bool IsUnset(const T& /*value*/) {
    return false;
}
template <typename T> bool IsUnset(const std::optional<T>& value) {
    return !value.has_value();
}
/// @}

template <typename T>
void Writer::operator()(std::string_view key, T& value, std::string_view /*description*/,
                        std::string_view /*unit*/, const FieldRange& /*range*/) {
    if (omitUnset_ && IsUnset(value)) {
        return;
    }
    json_[std::string(key)] = ToJson(value, compact_);
}

}  // namespace terraingen::config
