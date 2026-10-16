/// @file
/// @brief The Neukum et al. (2001) lunar chronology and production function, and its extension to
/// small craters.
#pragma once

#include "terraingen/distribution/log_polynomial.h"
#include "terraingen/distribution/spliced.h"

namespace terraingen {

/// @brief Oldest surface age, in Gyr, accepted by `NeukumLunarChronology`.
inline constexpr double kMaxLunarAgeGyr = 4.6;

/// @brief Neukum et al. (2001) lunar chronology, N1(T) = 5.44e-14 (e^(6.93 T) - 1) + 8.38e-4 T
/// per km^2. A `Chronology` for `ProductionSurface` and `SteadyStateAges`.
/// @param ageGyr Surface age T in Gyr.
/// @return Craters at least 1 km across per m^2.
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxLunarAgeGyr`.
double NeukumLunarChronology(double ageGyr);

/// @brief dN1/dT of `NeukumLunarChronology`: the formation rate of craters at least 1 km across.
/// @param ageGyr Time before present in Gyr.
/// @return Craters per m^2 per Gyr.
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxLunarAgeGyr`.
double NeukumLunarChronologyRate(double ageGyr);

/// @brief Neukum et al. (2001) lunar production function (eq. 2, table 1 "new" N(D)), valid from
/// 10 m to 300 km.
/// @return A `ProductionRate` per m^2 per Gyr at the present-day flux.
LogPolynomialSfd NeukumProductionFunction();

/// @brief `NeukumProductionFunction` extended below 10 m as a cumulative power law of slope -2.85,
/// continuous at 10 m.
/// @return A `ProductionRate` valid from 2.5 to 300000 m.
SplicedSfd ExtendedNeukumProductionFunction();

}  // namespace terraingen
