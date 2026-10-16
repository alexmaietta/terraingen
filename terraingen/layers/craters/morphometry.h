/// @file
/// @brief Fresh crater morphometry and complex crater shapes fitted to the Moon and Mars.
#pragma once

#include "terraingen/layers/craters/degradation/state.h"
#include "terraingen/layers/craters/profiles/complex.h"

namespace terraingen {

/// @brief Rim height / diameter of fresh lunar craters (Fassett et al. 2022).
inline constexpr double kLunarFreshRimHeightRatio = 0.036822095;

/// @brief Fassett et al. (2022) initial rim-to-floor depth/diameter of fresh lunar craters, 0.10
/// up to 10 m rising in steps to 0.21 above 400 m.
/// @param diameter Crater diameter in metres.
/// @return The depth/diameter ratio.
/// @throws std::domain_error if `diameter` is not positive and finite.
double LunarFreshCraterDepthRatio(double diameter);

/// @brief Diameter in metres at which fresh lunar craters become complex (Pike 1977).
inline constexpr double kLunarComplexCraterDiameter = 15000.0;

/// @brief The morphometry of a fresh lunar crater: `LunarFreshCraterDepthRatio` and
/// `kLunarFreshRimHeightRatio` below `kLunarComplexCraterDiameter`, Pike's complex fits above. A
/// `FreshMorphometry` for the degradation models.
/// @param diameter Crater diameter in metres.
/// @return Depth and rim height over diameter.
/// @throws std::domain_error if `diameter` is not positive and finite.
CraterMorphometry LunarFreshCraterMorphometry(double diameter);

/// @brief The shape of fresh lunar complex craters for `ComplexCraterProfile`, from
/// `kLunarComplexCraterDiameter`, with central peak fits held at 51 km beyond.
/// @return Floor diameter, peak height and peak volume fits.
ComplexCraterShape LunarComplexCraterShape();

/// @brief Diameter in metres at which fresh Martian craters become complex (Garvin et al. 2003).
inline constexpr double kMarsComplexCraterDiameter = 7000.0;

/// @brief The morphometry of a fresh Martian crater: depth/diameter at most 0.23 and rim
/// height/diameter at most 0.03, falling to Garvin et al.'s (2003) fits for large and complex
/// craters. A `FreshMorphometry` for the degradation models.
/// @param diameter Crater diameter in metres.
/// @return Depth and rim height over diameter.
/// @throws std::domain_error if `diameter` is not positive and finite.
CraterMorphometry MarsFreshCraterMorphometry(double diameter);

/// @brief The shape of fresh Martian complex craters for `ComplexCraterProfile`, from
/// `kMarsComplexCraterDiameter`, with peak fits held at 100 km beyond and the lunar floor fit.
/// @return Floor diameter, peak height and peak diameter fits.
ComplexCraterShape MarsComplexCraterShape();

}  // namespace terraingen
