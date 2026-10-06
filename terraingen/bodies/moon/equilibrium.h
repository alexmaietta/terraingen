#pragma once

#include "terraingen/distribution/power_law.h"

namespace terraingen {

/// Trask (1966) lunar equilibrium, N(>=D) = 10^-1.1 D^-2 (the same in km/km^2 and m/m^2). The DSNE
/// uses it as the upper limit on the density of craters smaller than about 100-200 m on any
/// surface.
///
/// References: N. J. Trask, in Ranger VIII and IX Part II, JPL Technical Report 32-800, 1966; NASA
/// SLS-SPEC-159 (DSNE) Revision I, Section 3.4.1.1.
PowerLawSfd TraskEquilibrium();

/// Hartmann (1984) lunar equilibrium, N(>=D) = 10^-1.14 D_km^-1.83 per km^2, as given by Fassett et
/// al. (2022, eq. 3b).
///
/// Reference: C. I. Fassett et al., "Topographic Diffusion Revisited", JGR Planets 127, 2022.
/// https://doi.org/10.1029/2022JE007510
PowerLawSfd HartmannEquilibrium();

}  // namespace terraingen
