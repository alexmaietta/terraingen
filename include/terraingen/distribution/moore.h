// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Moore's lunar terrain crater distributions for Lunar Roving Vehicle trafficability.
#pragma once

#include "terraingen/distribution/piecewise_power_law.h"

namespace terraingen {

/// Moore's terrain-specific cumulative crater distributions for Lunar Roving Vehicle
/// trafficability, N per m^2 with D in meters.
enum class MooreTerrain {
    RoughMare,        ///< 10^-1 D^-2 below 100 m, 10 D^-3 above.
    SmoothMare,       ///< 10^-1 D^-2 below 40 m, then 10^0.602 D^-3, 10^-2.038 D^-1.68, 10 D^-3.
    Uplands,          ///< 10^-1 D^-2 below 1 km, 10^2 D^-3 above.
    HeavilyCratered,  ///< 10^-1 D^-2 up to 10 km.
};

/// Moore's `Density` distribution for `terrain`, accepting the rounded coefficients' joins within
/// 0.5%.
PiecewisePowerLawSfd MooreDistribution(MooreTerrain terrain);

}  // namespace terraingen
