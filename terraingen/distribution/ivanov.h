/// @file
/// @brief Ivanov's (2001) Martian chronology and production function.
#pragma once

#include "terraingen/distribution/log_polynomial.h"

namespace terraingen {

/// @brief Oldest surface age, in Gyr, accepted by `IvanovMarsChronology`.
inline constexpr double kMaxMarsAgeGyr = 4.6;

/// @brief Ivanov's (2001) Martian chronology, N1(T) = 2.68e-14 (e^(6.93 T) - 1) + 4.13e-4 T per
/// km^2. A `Chronology` for `ProductionSurface` and `SteadyStateAges`.
/// @param ageGyr Surface age T in Gyr.
/// @return Craters at least 1 km across per m^2.
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxMarsAgeGyr`.
double IvanovMarsChronology(double ageGyr);

/// @brief dN1/dT of `IvanovMarsChronology`.
/// @param ageGyr Time before present in Gyr.
/// @return Craters at least 1 km across per m^2 per Gyr.
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxMarsAgeGyr`.
double IvanovMarsChronologyRate(double ageGyr);

/// @brief Ivanov's (2001) Martian production function, valid from 15 m to 362 km.
/// @return A `ProductionRate` per m^2 per Gyr at the present-day flux.
LogPolynomialSfd IvanovMarsProductionFunction();

}  // namespace terraingen
