/// @file
/// @brief The exponential-plus-linear chronology form of Neukum (lunar) and Ivanov (Martian).
#pragma once

namespace terraingen::detail {

/// A chronology N1(T) = `burst` (e^(`decay` T) - 1) + `linear` T per km^2: craters at least 1 km
/// across on a surface T Gyr old.
struct ExponentialChronology {
    double burst = 0.0;         ///< Coefficient of the exponential term, per km^2.
    double decay = 0.0;         ///< Rate of the exponential term, per Gyr.
    double linear = 0.0;        ///< Present-day flux, per km^2 per Gyr.
    double maxAgeGyr = 0.0;     ///< Oldest age accepted, in Gyr.
    const char* ageError = "";  ///< Message for an age outside [0, `maxAgeGyr`].

    /// N1 per m^2 at `ageGyr` Gyr.
    /// @throws std::domain_error with `ageError` unless 0 <= `ageGyr` <= `maxAgeGyr`.
    double Count(double ageGyr) const;

    /// dN1/dT per m^2 per Gyr at `ageGyr` Gyr before present.
    /// @throws std::domain_error as `Count`.
    double Rate(double ageGyr) const;
};

}  // namespace terraingen::detail
