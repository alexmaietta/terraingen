/// @file
/// @brief Lunar topographic diffusivities of Fassett and Thomson (2014) and Fassett et al. (2022).
#pragma once

#include "terraingen/layers/craters/degradation/diffusion.h"

namespace terraingen {

/// @brief The lunar diffusivity of Fassett and Thomson (2014), 5.5 m^2/Myr for every size.
/// @return The constant diffusivity.
PowerLawDiffusivity FassettThomson2014Diffusivity();

/// @brief Fassett et al.'s (2022) diffusivity holding the Hartmann equilibrium, kappa = 3.6
/// D^0.87 m^2/Myr with D in km, held at its 5 m value below 5 m.
/// @return The size-dependent diffusivity.
PowerLawDiffusivity Fassett2022HartmannDiffusivity();

/// @brief As `Fassett2022HartmannDiffusivity` for the Trask equilibrium: kappa = 3.1 D^1.05
/// m^2/Myr.
/// @return The size-dependent diffusivity.
PowerLawDiffusivity Fassett2022TraskDiffusivity();

/// @brief The diffusion age kappa t Fassett and Thomson (2014, eq. 6) calibrate against crater
/// age, a quintic in t clamped at 0.
/// @param ageGyr Crater age t in Gyr.
/// @return kappa t in m^2, >= 0.
double FassettThomson2014DiffusionAge(double ageGyr);

}  // namespace terraingen
