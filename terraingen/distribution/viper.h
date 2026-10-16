/// @file
/// @brief The VIPER environmental specification's crater distribution.
#pragma once

#include "terraingen/distribution/piecewise_power_law.h"

namespace terraingen {

/// @brief The VIPER environmental specification as used by synthterrain, N = 29174 D^-1.92 per
/// km^2 up to 80 m and 156228 D^-2.389 above (D in m), discontinuous at 80 m. For comparison only.
/// @return The `Density` distribution.
PiecewisePowerLawSfd ViperSpecification();

}  // namespace terraingen
