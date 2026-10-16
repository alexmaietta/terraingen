/// @file
/// @brief The Trask (1966) and Hartmann (1984) lunar crater equilibrium densities.
#pragma once

#include "terraingen/distribution/power_law.h"

namespace terraingen {

/// Trask (1966) lunar equilibrium, N(>=D) = 10^-1.1 D^-2, the DSNE's upper limit on the density
/// of small craters; a `Density` per m^2 with D in metres, valid at every diameter.
PowerLawSfd TraskEquilibrium();

/// Hartmann (1984) lunar equilibrium, N(>=D) = 10^-1.14 D_km^-1.83 per km^2; returned as a
/// `Density` per m^2 with D in metres, valid at every diameter.
PowerLawSfd HartmannEquilibrium();

}  // namespace terraingen
