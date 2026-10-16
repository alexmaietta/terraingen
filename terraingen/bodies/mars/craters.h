/// @file
/// @brief Fresh Martian crater morphometry.
#pragma once

#include "terraingen/layers/craters/degradation/state.h"
#include "terraingen/layers/craters/profiles/complex.h"

namespace terraingen {

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
