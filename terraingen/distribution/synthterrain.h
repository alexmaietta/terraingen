/// @file
/// @brief The boulder size distributions synthterrain offers.
#pragma once

#include "terraingen/distribution/power_law.h"

#include <memory>

namespace terraingen {

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
