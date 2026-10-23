/// @file
/// @brief Checking configuration numbers against the ranges their field lists declare.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/checks.h"
#include "terraingen/config/fields/common.h"
#include "terraingen/config/fields/sources.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen::config {

/// What a range allows, as an error message says it: "positive", "from 1 to 4096 deg".
/// @param unit The unit symbol given after the bounds, or empty.
std::string DescribeRange(const FieldRange& range, std::string_view unit = {});

/// Checks that a number is finite and within its field's range.
/// @param unit The field's unit symbol, for the message, or empty.
/// @throws ConfigError at `path`, such as "must be positive, got -2", otherwise.
void CheckRange(double value, const FieldRange& range, std::string_view unit,
                const std::string& path);

class RangeChecker;

/// Whether `Fields(checker, value)` is callable with a `RangeChecker`.
template <typename T, typename = void> struct HasRangedFieldsTrait : std::false_type {};
template <typename T>
struct HasRangedFieldsTrait<
    T, std::void_t<decltype(Fields(std::declval<RangeChecker&>(), std::declval<T&>()))>>
    : std::true_type {};

/// A struct whose `Fields` a `RangeChecker` walks.
template <typename T> inline constexpr bool HasRangedFields = HasRangedFieldsTrait<T>::value;

/// @name CheckRanges
/// Checks every number in a configuration value of the overload's type, at `path` or below it,
/// against `range` for numbers and their fields' own ranges for structs.
/// @throws ConfigError at the first number out of its range.
/// @{
template <typename T>
void CheckRanges(T& value, const std::string& path, std::string_view unit, const FieldRange& range);
template <typename T>
void CheckRanges(std::optional<T>& value, const std::string& path, std::string_view unit,
                 const FieldRange& range);
template <typename T>
void CheckRanges(Boxed<T>& value, const std::string& path, std::string_view unit,
                 const FieldRange& range);
template <typename T>
void CheckRanges(std::vector<T>& value, const std::string& path, std::string_view unit,
                 const FieldRange& range);
template <typename... Ts>
void CheckRanges(std::variant<Ts...>& value, const std::string& path, std::string_view unit,
                 const FieldRange& range);
void CheckRanges(UpsamplingSpec& value, const std::string& path, std::string_view unit,
                 const FieldRange& range);
void CheckRanges(std::array<double, 2>& value, const std::string& path, std::string_view unit,
                 const FieldRange& range);
/// @}

/// Walks the fields of one object at `path`, checking each against its range (`CheckRanges`).
class RangeChecker {
  public:
    /// A checker of the object at `path`.
    explicit RangeChecker(std::string path) : path_(std::move(path)) {}

    /// Checks a field; `description` is for `ConfigSchema`.
    /// @throws ConfigError at the key's path, or below it, for a number out of its range.
    template <typename T>
    void operator()(std::string_view key, T& value, std::string_view /*description*/ = {},
                    std::string_view unit = {}, const FieldRange& range = {}) {
        CheckRanges(value, Child(path_, key), unit, range);
    }

  private:
    std::string path_;
};

template <typename T>
void CheckRanges(T& value, const std::string& path, std::string_view unit,
                 const FieldRange& range) {
    if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        CheckRange(static_cast<double>(value), range, unit, path);
    } else if constexpr (HasRangedFields<T>) {
        RangeChecker checker(path);
        Fields(checker, value);
    }
}

template <typename T>
void CheckRanges(std::optional<T>& value, const std::string& path, std::string_view unit,
                 const FieldRange& range) {
    if (value) {
        CheckRanges(*value, path, unit, range);
    }
}

template <typename T>
void CheckRanges(Boxed<T>& value, const std::string& path, std::string_view unit,
                 const FieldRange& range) {
    CheckRanges(*value, path, unit, range);
}

template <typename T>
void CheckRanges(std::vector<T>& value, const std::string& path, std::string_view unit,
                 const FieldRange& range) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        CheckRanges(value[i], Element(path, i), unit, range);
    }
}

template <typename... Ts>
void CheckRanges(std::variant<Ts...>& value, const std::string& path, std::string_view unit,
                 const FieldRange& range) {
    std::visit([&](auto& alternative) { CheckRanges(alternative, path, unit, range); }, value);
}

inline void CheckRanges(std::array<double, 2>& value, const std::string& path,
                        std::string_view unit, const FieldRange& range) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        CheckRange(value[i], range, unit, Element(path, i));
    }
}

inline void CheckRanges(UpsamplingSpec& value, const std::string& path, std::string_view /*unit*/,
                        const FieldRange& /*range*/) {
    if (value.type == "diamond_square") {
        RangeChecker checker(path);
        Fields(checker, value.diamondSquare);
    }
}

}  // namespace terraingen::config
