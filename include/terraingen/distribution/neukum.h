// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The Neukum et al. (2001) lunar chronology and production function, extended to small D.
#pragma once

#include "terraingen/distribution/log_polynomial.h"
#include "terraingen/distribution/spliced.h"

namespace terraingen {

/// Oldest surface age, in Gyr, accepted by `NeukumLunarChronology`.
inline constexpr double kMaxLunarAgeGyr = 4.6;

/// Neukum et al. (2001) lunar chronology, N1(T) = 5.44e-14 (e^(6.93 T) - 1) + 8.38e-4 T per km^2:
/// craters at least 1 km across per m^2 on a surface `ageGyr` Gyr old. A `Chronology` for
/// `ProductionSurface` and `SteadyStateAges`.
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxLunarAgeGyr`.
double NeukumLunarChronology(double ageGyr);

/// dN1/dT of `NeukumLunarChronology`, craters at least 1 km across formed per m^2 per Gyr at
/// `ageGyr` Gyr before present.
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxLunarAgeGyr`.
double NeukumLunarChronologyRate(double ageGyr);

/// Neukum et al. (2001) lunar production function (eq. 2, table 1 "new" N(D)), valid from 10 m
/// to 300 km; a `ProductionRate` per m^2 per Gyr at the present-day flux.
LogPolynomialSfd NeukumProductionFunction();

/// `NeukumProductionFunction` extended below 10 m as a cumulative power law of slope -2.85,
/// continuous at 10 m; a `ProductionRate` valid from 2.5 to 300000 m.
SplicedSfd ExtendedNeukumProductionFunction();

}  // namespace terraingen
