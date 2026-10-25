// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The boulder size distributions synthterrain offers.
#pragma once

#include "terraingen/distribution/power_law.h"

namespace terraingen {

/// The boulder distributions synthterrain offers, N per m^2 with D in meters, for
/// comparison with it.
enum class SynthterrainBoulders {
    InterCrater,         ///< 0.0001 D^-1.75457, "low blockiness".
    Haworth,             ///< 0.002 D^-2.6607, "high blockiness".
    ViperSpecification,  ///< 0.0003 D^-2.482, the VIPER Environmental Specification.
};

/// The synthterrain boulder distribution `boulders` as a power-law `Density`, valid at every
/// diameter.
/// @throws std::invalid_argument for an unknown distribution.
PowerLawSfd SynthterrainBoulderDistribution(SynthterrainBoulders boulders);

}  // namespace terraingen
