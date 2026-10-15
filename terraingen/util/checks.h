/// @file
/// @brief Predicates and argument checks for numeric parameters.
#pragma once

#include <cmath>
#include <string_view>

namespace terraingen {

/// @brief Whether a value is finite and greater than zero.
/// @param value The value to test.
/// @return True for a finite `value` > 0.
inline bool IsPositive(double value) {
    return std::isfinite(value) && value > 0.0;
}

/// @brief Whether a value is finite and not negative.
/// @param value The value to test.
/// @return True for a finite `value` >= 0.
inline bool IsNonNegative(double value) {
    return std::isfinite(value) && value >= 0.0;
}

/// @brief Whether a value lies in [0, 1].
/// @param value The value to test.
/// @return True for 0 <= `value` <= 1; false for NaN.
inline bool IsInUnitInterval(double value) {
    return value >= 0.0 && value <= 1.0;
}

/// @brief Checks that a value is positive and finite.
/// @param value The value to check.
/// @param what The value's name, which starts the error message.
/// @throws std::invalid_argument "<what> must be positive and finite" otherwise.
void RequirePositive(double value, std::string_view what);

/// @brief Checks that a value is finite and not negative.
/// @param value The value to check.
/// @param what The value's name, which starts the error message.
/// @throws std::invalid_argument "<what> must be finite and not negative" otherwise.
void RequireNonNegative(double value, std::string_view what);

/// @brief Checks that a value is finite.
/// @param value The value to check.
/// @param what The value's name, which starts the error message.
/// @throws std::invalid_argument "<what> must be finite" for an infinite or NaN `value`.
void RequireFinite(double value, std::string_view what);

/// @brief Checks that a value lies in [0, 1].
/// @param value The value to check.
/// @param what The value's name, which starts the error message.
/// @throws std::invalid_argument "<what> must be in [0, 1]" otherwise.
void RequireUnitInterval(double value, std::string_view what);

}  // namespace terraingen
