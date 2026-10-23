/// @file
/// @brief Predicates and argument checks for numeric parameters; each check's `what` names the
/// value and starts its error message.
#pragma once

#include <cmath>
#include <string_view>

namespace terraingen {

/// Whether a value is finite and greater than zero.
inline bool IsPositive(double value) {
    return std::isfinite(value) && value > 0.0;
}

/// Whether a value is finite and not negative.
inline bool IsNonNegative(double value) {
    return std::isfinite(value) && value >= 0.0;
}

/// Whether a value lies in [0, 1]; false for NaN.
inline bool IsInUnitInterval(double value) {
    return value >= 0.0 && value <= 1.0;
}

/// Checks that a value is positive and finite.
/// @throws std::invalid_argument "<what> must be positive and finite" otherwise.
void RequirePositive(double value, std::string_view what);

/// Checks that a value is finite and not negative.
/// @throws std::invalid_argument "<what> must be finite and not negative" otherwise.
void RequireNonNegative(double value, std::string_view what);

/// Checks that a value is finite.
/// @throws std::invalid_argument "<what> must be finite" for an infinite or NaN `value`.
void RequireFinite(double value, std::string_view what);

/// Checks that a value lies in [0, 1].
/// @throws std::invalid_argument "<what> must be in [0, 1]" otherwise.
void RequireUnitInterval(double value, std::string_view what);


/// Checks a feature's diameter, the argument of a model defined by size.
/// @throws std::domain_error "Diameter must be positive and finite" otherwise.
void RequireDiameter(double diameter);

}  // namespace terraingen
