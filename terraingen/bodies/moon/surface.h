/// @file
/// @brief The DSNE model of a lunar surface of a given age.
#pragma once

#include "terraingen/bodies/surface_model.h"

namespace terraingen {

/// @brief The DSNE construction of a lunar surface: density min(`ExtendedNeukumProductionFunction`
/// surface, `TraskEquilibrium`), valid from 2.5 m to 300 km, with `SteadyStateAges` from the same
/// two distributions.
///
/// Reference: NASA SLS-SPEC-159 (DSNE) Revision I, Section 3.4.1.1.
/// @param ageGyr The surface age in Gyr.
/// @return The surface's crater density and ages.
/// @throws std::domain_error if `ageGyr` is outside [0, `kMaxLunarAgeGyr`].
SurfaceModel LunarSurface(double ageGyr);

}  // namespace terraingen
