/// @file
/// @brief Lunar topographic diffusivities of Fassett and Thomson (2014) and Fassett et al. (2022).
#pragma once

#include "terraingen/layers/craters/degradation/diffusion.h"

namespace terraingen {

/// The lunar diffusivity of Fassett and Thomson (2014), 5.5 m^2/Myr for every size.
PowerLawDiffusivity FassettThomson2014Diffusivity();

/// Fassett et al.'s (2022) diffusivity holding the Hartmann equilibrium, kappa = 3.6
/// D^0.87 m^2/Myr with D in km, held at its 5 m value below 5 m. Pair it with steady-state ages
/// on the Hartmann equilibrium; with Trask's it wears craters far too fast.
PowerLawDiffusivity Fassett2022HartmannDiffusivity();

/// As `Fassett2022HartmannDiffusivity` for the Trask equilibrium: kappa = 3.1 D^1.05 m^2/Myr.
/// Pair it with steady-state ages on the Trask equilibrium.
PowerLawDiffusivity Fassett2022TraskDiffusivity();

/// The diffusion age kappa t in m^2 that Fassett and Thomson (2014, eq. 6) calibrate against
/// crater age `ageGyr` (Gyr): a quintic in t clamped at 0.
double FassettThomson2014DiffusionAge(double ageGyr);

}  // namespace terraingen
