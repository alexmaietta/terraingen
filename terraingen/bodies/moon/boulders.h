/// @file
/// @brief Lunar boulder size distributions from engineering models and synthterrain.
#pragma once

#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/tabulated.h"

#include <memory>

namespace terraingen {

/// @brief Which of the minimum, nominal and maximum curves of an engineering model to use.
enum class SpecificationCurve { Minimum, Nominal, Maximum };

/// @brief Blocks per m^2 at least D metres across between craters: NASA SP-8023 Figure 7,
/// digitized, from 0.03 mm to about 6 to 9 m.
/// @param curve Which curve.
/// @return The tabulated `Density` distribution.
/// @throws std::invalid_argument for an unknown curve.
std::shared_ptr<TabulatedSfd> Sp8023IntercraterBlocks(SpecificationCurve curve);

/// @brief Blocks per m^2 at least D metres across from the rim to two radii of blocky craters:
/// NASA SP-8023 Figure 9, digitized, from 0.85 to 1.4 m up to 7 to 24 m.
/// @param curve Which curve.
/// @return The tabulated `Density` distribution.
/// @throws std::invalid_argument for an unknown curve.
std::shared_ptr<TabulatedSfd> Sp8023CraterBlocks(SpecificationCurve curve);

/// @brief The boulder distributions synthterrain offers, N per m^2 with D in metres, for
/// comparison with it.
enum class SynthterrainBoulders {
    InterCrater,         ///< 0.0001 D^-1.75457, "low blockiness".
    Haworth,             ///< 0.002 D^-2.6607, "high blockiness".
    ViperSpecification,  ///< 0.0003 D^-2.482, the VIPER Environmental Specification.
};

/// @brief The synthterrain boulder distribution `boulders`, valid at every diameter.
/// @param boulders Which distribution.
/// @return The power-law `Density` distribution.
/// @throws std::invalid_argument for an unknown distribution.
std::shared_ptr<PowerLawSfd> SynthterrainBoulderDistribution(SynthterrainBoulders boulders);

}  // namespace terraingen
