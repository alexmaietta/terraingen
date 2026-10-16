/// @file
/// @brief Fresh lunar crater morphometry and lunar topographic diffusivities.
#pragma once

#include "terraingen/layers/craters/degradation/diffusion.h"
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
