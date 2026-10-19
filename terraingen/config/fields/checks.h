/// @file
/// @brief JSON paths and value checks that report configuration errors at a path.
#pragma once

#include "terraingen/config/json.h"

#include <cmath>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>

namespace terraingen::config {

/// The JSON path `path.key` of an object's member, or just `key` when `path` is empty.
inline std::string Child(const std::string& path, std::string_view key) {
    return path.empty() ? std::string(key) : path + "." + std::string(key);
}

/// The JSON path `path[index]` of an array's element.
inline std::string Element(const std::string& path, std::size_t index) {
    return path + "[" + std::to_string(index) + "]";
}

/// Checks that a configuration value is finite.
/// @throws ConfigError "must be finite" at `path` for an infinite or NaN `value`.
inline void CheckFinite(double value, const std::string& path) {
    if (!std::isfinite(value)) {
        throw ConfigError(path, "must be finite");
    }
}

/// Checks that a configuration string, such as a file path, is not empty.
/// @throws ConfigError "must not be empty" at `path` otherwise.
inline void CheckNonEmpty(const std::string& value, const std::string& path) {
    if (value.empty()) {
        throw ConfigError(path, "must not be empty");
    }
}

/// Runs a library check or build step `check()` and returns its result, reporting its errors at
/// the configuration path `path`.
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

/// Runs a step `read()` that reads a file, such as a GeoTIFF or a mesh, and returns its result,
/// reporting any failure at the configuration path `path` of the file.
/// @throws ConfigError at `path` with the message of any exception `read` throws.
template <typename Read> auto AtFile(const std::string& path, Read read) {
    try {
        return read();
    } catch (const ConfigError&) {
        throw;
    } catch (const std::exception& error) {
        throw ConfigError(path, error.what());
    }
}

}  // namespace terraingen::config
