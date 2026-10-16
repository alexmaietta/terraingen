/// @file
/// @brief Martian surfaces of a given age.
#pragma once

#include "terraingen/bodies/surface_model.h"

namespace terraingen {

/// @brief A Martian surface with no erosion: `IvanovMarsProductionFunction` scaled by
/// `IvanovMarsChronology`, valid from 15 m to 362 km, with `UniformAges`; suits surfaces younger
/// than about 3 Gyr. No equilibrium cap is applied.
/// @param ageGyr Surface age in Gyr.
/// @return The surface's crater density and ages.
/// @throws std::domain_error if `ageGyr` is outside (0, `kMaxMarsAgeGyr`].
SurfaceModel MarsSurface(double ageGyr);

}  // namespace terraingen
