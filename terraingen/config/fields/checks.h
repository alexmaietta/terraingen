/// @file
/// @brief JSON paths and value checks that report configuration errors at a path.
#pragma once

#include "terraingen/config/json.h"
#include "terraingen/util/checks.h"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace terraingen::config {

/// @brief The JSON path of a member of an object.
/// @param path The object's path; empty for the document root.
/// @param key The member's name.
/// @return `path.key`, or just `key` at the root.
inline std::string Child(const std::string& path, std::string_view key) {
    return path.empty() ? std::string(key) : path + "." + std::string(key);
}

/// @brief The JSON path of an element of an array.
/// @param path The array's path.
/// @param index The element's index.
/// @return `path[index]`.
inline std::string Element(const std::string& path, std::size_t index) {
    return path + "[" + std::to_string(index) + "]";
}

/// @brief Checks that a configuration value is positive and finite.
/// @param value The value to check.
/// @param path The value's JSON path.
/// @throws ConfigError "must be positive" at `path` otherwise.
inline void RequirePositive(double value, const std::string& path) {
    if (!IsPositive(value)) {
        throw ConfigError(path, "must be positive");
    }
}

/// @brief Checks that a configuration value is finite and not negative.
/// @param value The value to check.
/// @param path The value's JSON path.
/// @throws ConfigError "must not be negative" at `path` otherwise.
inline void RequireNonNegative(double value, const std::string& path) {
    if (!IsNonNegative(value)) {
        throw ConfigError(path, "must not be negative");
    }
}

/// @brief Checks that a configuration value is finite.
/// @param value The value to check.
/// @param path The value's JSON path.
/// @throws ConfigError "must be finite" at `path` for an infinite or NaN `value`.
inline void RequireFinite(double value, const std::string& path) {
    if (!std::isfinite(value)) {
        throw ConfigError(path, "must be finite");
    }
}

/// @brief Checks that a configuration string, such as a file path, is not empty.
/// @param value The string to check.
/// @param path The value's JSON path.
/// @throws ConfigError "must not be empty" at `path` otherwise.
inline void RequireNonEmpty(const std::string& value, const std::string& path) {
    if (value.empty()) {
        throw ConfigError(path, "must not be empty");
    }
}

/// @brief Runs a library check or build step, reporting its errors at a configuration path.
/// @param path The JSON path of what `check` checks.
/// @param check A callable taking no arguments.
/// @return What `check` returns.
/// @throws ConfigError at `path` with the message of a std::invalid_argument or std::domain_error
/// that `check` throws.
template <typename Check> auto At(const std::string& path, Check check) {
    try {
        return check();
    } catch (const std::invalid_argument& error) {
        throw ConfigError(path, error.what());
    } catch (const std::domain_error& error) {
        throw ConfigError(path, error.what());
    }
}

}  // namespace terraingen::config
