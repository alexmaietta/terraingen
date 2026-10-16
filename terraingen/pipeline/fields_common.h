/// @file
/// @brief What every configuration field list shares.
///
/// Each configuration struct has a `Fields(IO& io, Struct& value)` that calls
/// `io(key, member, description, unit)` for each JSON key in order; the same list is read, written
/// and described (`ConfigSchema`) by passing a different `IO` (`Reader`, `Writer` or a schema
/// writer). Each alternative of a variant also has a `SpecName` next to its `Fields`.
#pragma once

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace terraingen::config {

/// @brief The JSON "type" of the variant alternative `Spec` (`kName`) and what it is
/// (`kDescription`).
template <typename Spec> struct SpecName;

/// @brief Reads or writes a bound that is unset when infinite, as an optional field.
/// @param io The field reader, writer or describer.
/// @param key The JSON key.
/// @param bound The bound.
/// @param description What the field holds.
/// @param unit The bound's unit symbol, or empty.
/// @param unset The bound when the field is unset: +infinity or -infinity.
template <typename IO>
void OptionalBound(IO& io, std::string_view key, double& bound, std::string_view description,
                   std::string_view unit = {},
                   double unset = std::numeric_limits<double>::infinity()) {
    std::optional<double> value;
    if (std::isfinite(bound)) {
        value = bound;
    }
    io(key, value, description, unit);
    bound = value.value_or(unset);
}

/// @brief The description of an output file's path.
/// @param file What the file is, such as "GeoTIFF file to write".
/// @return `file` and where relative paths resolve.
inline std::string OutputPathHelp(std::string_view file) {
    return std::string(file) + "; relative paths resolve against the output folder (the CLI's "
                               "--output-dir), else the configuration's folder.";
}

/// @brief The description of an input file's path.
/// @param file What the file is, such as "GeoTIFF to read the heights from".
/// @return `file` and where relative paths resolve.
inline std::string InputPathHelp(std::string_view file) {
    return std::string(file) + "; relative paths resolve against the configuration's folder.";
}

}  // namespace terraingen::config
