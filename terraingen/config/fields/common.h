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

/// The JSON "type" of the variant alternative `Spec` (`kName`) and what it is (`kDescription`).
template <typename Spec> struct SpecName;

/// The values a numeric field, or each number of a list, may take, which `ConfigSchema` gives
/// editors and the reference; validation still checks them, with the rules that tie fields
/// together. Unset bounds are open.
struct FieldRange {
    std::optional<double> minimum;  ///< Smallest value, in the field's unit.
    std::optional<double> maximum;  ///< Largest value, in the field's unit.
    bool exclusiveMinimum = false;  ///< `minimum` itself is out.
    bool exclusiveMaximum = false;  ///< `maximum` itself is out.

    /// Whether the range sets a bound.
    bool Bounded() const {
        return minimum.has_value() || maximum.has_value();
    }
};

/// Values of at least `minimum`.
inline FieldRange AtLeast(double minimum) {
    return {minimum, std::nullopt};
}

/// Values above `minimum`; `Above(0)` for positive values.
inline FieldRange Above(double minimum) {
    return {minimum, std::nullopt, true, false};
}

/// Values of at most `maximum`.
inline FieldRange AtMost(double maximum) {
    return {std::nullopt, maximum};
}

/// Values from `minimum` to `maximum`, both included.
inline FieldRange Between(double minimum, double maximum) {
    return {minimum, maximum};
}

/// Values from `minimum`, included, to below `maximum`.
inline FieldRange FromBelow(double minimum, double maximum) {
    return {minimum, maximum, false, true};
}

/// Values above `minimum` up to `maximum`, included.
inline FieldRange AboveUpTo(double minimum, double maximum) {
    return {minimum, maximum, true, false};
}

/// Values above `minimum` and below `maximum`.
inline FieldRange Inside(double minimum, double maximum) {
    return {minimum, maximum, true, true};
}

/// Shares from 0 to 1.
inline FieldRange UnitInterval() {
    return Between(0.0, 1.0);
}

/// Reads or writes a bound that is unset when infinite, as an optional field.
/// @param unit The bound's unit symbol, or empty.
/// @param unset The bound when the field is unset: +infinity or -infinity.
template <typename IO>
void OptionalBound(IO& io, std::string_view key, double& bound, std::string_view description,
                   std::string_view unit = {},
                   double unset = std::numeric_limits<double>::infinity(),
                   const FieldRange& range = {}) {
    std::optional<double> value;
    if (std::isfinite(bound)) {
        value = bound;
    }
    io(key, value, description, unit, range);
    bound = value.value_or(unset);
}

/// The description of an output file's path: `file`, such as "GeoTIFF file to write", and where
/// relative paths resolve.
inline std::string OutputPathHelp(std::string_view file) {
    return std::string(file) + "; relative paths resolve against the output folder (the CLI's "
                               "--output-dir), else the configuration's folder.";
}

/// The description of an input file's path: `file`, such as "GeoTIFF to read the heights from",
/// and where relative paths resolve.
inline std::string InputPathHelp(std::string_view file) {
    return std::string(file) + "; relative paths resolve against the configuration's folder.";
}

}  // namespace terraingen::config
