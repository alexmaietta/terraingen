#pragma once

#include "terraingen/distributions/piecewise_power_law.h"

namespace terraingen {

/// Moore's terrain-specific cumulative distributions for Lunar Roving Vehicle trafficability (USGS,
/// "Section C"), N per m^2 with D in metres.
enum class MooreTerrain {
    RoughMare,        ///< 10^-1 D^-2 below 100 m, 10 D^-3 above.
    SmoothMare,       ///< 10^-1 D^-2 below 40 m, then 10^0.602 D^-3, 10^-2.038 D^-1.68, 10 D^-3.
    Uplands,          ///< 10^-1 D^-2 below 1 km, 10^2 D^-3 above.
    HeavilyCratered,  ///< 10^-1 D^-2 up to 10 km.
};

/// Moore's distribution for `terrain` (H. J. Moore, USGS Section C, c. 1970). The published
/// coefficients are rounded, so segments agree at their joins only to within 0.2%; the presets
/// accept joins within 0.5%.
PiecewisePowerLawSfd MooreDistribution(MooreTerrain terrain);

/// The VIPER environmental specification (VIPER-MSE-SPEC-001, 2021) as used by synthterrain:
/// N = 29174 D^-1.92 per km^2 for D <= 80 m and 156228 D^-2.389 above (D in m). Kept for comparison
/// with synthterrain; its segments disagree by a factor of 1.46 at 80 m, and Allan et al. (2017)
/// found it predicts too many craters against a NAC image.
PiecewisePowerLawSfd ViperSpecification();

}  // namespace terraingen
