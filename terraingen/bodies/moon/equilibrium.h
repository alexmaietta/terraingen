/// @file
/// @brief Lunar crater equilibrium densities.
#pragma once

#include "terraingen/distribution/power_law.h"

namespace terraingen {

/// @brief Trask (1966) lunar equilibrium, N(>=D) = 10^-1.1 D^-2, the DSNE's upper limit on the
/// density of small craters.
/// @return A `Density` per m^2 with D in metres, valid at every diameter.
PowerLawSfd TraskEquilibrium();

/// @brief Hartmann (1984) lunar equilibrium, N(>=D) = 10^-1.14 D_km^-1.83 per km^2.
/// @return A `Density` per m^2 with D in metres, valid at every diameter.
PowerLawSfd HartmannEquilibrium();

}  // namespace terraingen
