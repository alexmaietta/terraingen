// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The VIPER environmental specification's crater distribution.
#pragma once

#include "terraingen/distribution/piecewise_power_law.h"

namespace terraingen {

/// The VIPER environmental specification as used by synthterrain, a `Density` with N = 29174
/// D^-1.92 per km^2 up to 80 m and 156228 D^-2.389 above (D in m), discontinuous at 80 m. For
/// comparison only.
PiecewisePowerLawSfd ViperSpecification();

}  // namespace terraingen
