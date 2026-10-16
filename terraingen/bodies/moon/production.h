/// @file
/// @brief The Neukum et al. (2001) lunar production function and its extension to small craters.
#pragma once

#include "terraingen/bodies/moon/chronology.h"
#include "terraingen/distribution/log_polynomial.h"
#include "terraingen/distribution/production_surface.h"
#include "terraingen/distribution/spliced.h"

namespace terraingen {

/// @brief Neukum et al. (2001) lunar production function (eq. 2, table 1 "new" N(D)), valid from
/// 10 m to 300 km. See SCIENCE.md, "Lunar production function".
/// @return A `ProductionRate` per m^2 per Gyr at the present-day flux.
LogPolynomialSfd NeukumProductionFunction();

/// @brief `NeukumProductionFunction` extended below 10 m as a cumulative power law of slope -2.85,
/// continuous at 10 m. See SCIENCE.md, "Lunar production function".
/// @return A `ProductionRate` valid from 2.5 to 300000 m.
SplicedSfd ExtendedNeukumProductionFunction();

}  // namespace terraingen
