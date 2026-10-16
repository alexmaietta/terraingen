/// @file
/// @brief Fresh crater morphometry and complex crater shapes fitted to the Moon and Mars.
#pragma once

#include "terraingen/layers/craters/degradation/state.h"
#include "terraingen/layers/craters/profiles/complex.h"

namespace terraingen {

/// Rim height / diameter of fresh lunar craters (Fassett et al. 2022).
inline constexpr double kLunarFreshRimHeightRatio = 0.036822095;

/// The initial rim-to-floor depth/diameter of fresh lunar craters (Fassett et al. 2022): 0.10 up
/// to 10 m rising in steps to 0.21 above 400 m.
/// @param diameter In metres.
/// @throws std::domain_error if `diameter` is not positive and finite.
double LunarFreshCraterDepthRatio(double diameter);

/// Diameter in metres at which fresh lunar craters become complex (Pike 1977).
inline constexpr double kLunarComplexCraterDiameter = 15000.0;

/// Depth and rim height over diameter of a fresh lunar crater: `LunarFreshCraterDepthRatio` and
/// `kLunarFreshRimHeightRatio` below `kLunarComplexCraterDiameter`, Pike's complex fits above. A
/// `FreshMorphometry` for the degradation models.
/// @param diameter In metres.
/// @throws std::domain_error if `diameter` is not positive and finite.
CraterMorphometry LunarFreshCraterMorphometry(double diameter);

/// The floor diameter, peak height and peak volume fits of fresh lunar complex craters for
/// `ComplexCraterProfile`, from `kLunarComplexCraterDiameter`, with peak fits held at 51 km beyond.
ComplexCraterShape LunarComplexCraterShape();

/// Diameter in metres at which fresh Martian craters become complex (Garvin et al. 2003).
inline constexpr double kMarsComplexCraterDiameter = 7000.0;

/// Depth and rim height over diameter of a fresh Martian crater: at most 0.23 and 0.03, falling to
/// Garvin et al.'s (2003) fits for large and complex craters. A `FreshMorphometry` for the
/// degradation models.
/// @param diameter In metres.
/// @throws std::domain_error if `diameter` is not positive and finite.
CraterMorphometry MarsFreshCraterMorphometry(double diameter);

/// The floor diameter, peak height and peak diameter fits of fresh Martian complex craters for
/// `ComplexCraterProfile`, from `kMarsComplexCraterDiameter`, with peak fits held at 100 km beyond
/// and the lunar floor fit.
ComplexCraterShape MarsComplexCraterShape();

}  // namespace terraingen
