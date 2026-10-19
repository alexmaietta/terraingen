/// @file
/// @brief NASA SP-8023 lunar block size distributions, digitized.
#pragma once

#include "terraingen/distribution/tabulated.h"

namespace terraingen {

/// Which of the minimum, nominal and maximum curves of an engineering model to use.
enum class SpecificationCurve { Minimum, Nominal, Maximum };

/// Blocks per m^2 at least D metres across between craters, as a tabulated `Density`: NASA
/// SP-8023 Figure 7, digitized, from 0.03 mm to about 6 to 9 m.
/// @throws std::invalid_argument for an unknown curve.
TabulatedSfd Sp8023IntercraterBlocks(SpecificationCurve curve);

/// Blocks per m^2 at least D metres across from the rim to two radii of blocky craters, as a
/// tabulated `Density`: NASA SP-8023 Figure 9, digitized, from 0.85 to 1.4 m up to 7 to 24 m.
/// @throws std::invalid_argument for an unknown curve.
TabulatedSfd Sp8023CraterBlocks(SpecificationCurve curve);

}  // namespace terraingen
