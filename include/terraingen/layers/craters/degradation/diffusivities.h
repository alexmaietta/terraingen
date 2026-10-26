// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Lunar topographic diffusivities of Fassett and Thomson (2014) and Fassett et al. (2022).
#pragma once

#include "terraingen/layers/craters/ages/steady_state.h"
#include "terraingen/layers/craters/degradation/diffusion.h"

#include <functional>
#include <memory>

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

/// Diffusion ages that wear each crater of `profile` to `cutoffDepthRatio` at the end of its
/// equilibrium lifetime (Fassett et al. 2022, section 4, https://doi.org/10.1029/2022JE007510).
/// @param lifetimeGyr Equilibrium lifetime in Gyr of a crater diameter in meters; positive.
/// @throws std::invalid_argument for a null or empty argument, or a cutoff outside (0, 1).
DiffusionAgeModel EquilibriumDiffusionAges(std::shared_ptr<const DiffusedProfile> profile,
                                           FreshMorphometry fresh,
                                           std::function<double(double)> lifetimeGyr,
                                           double cutoffDepthRatio = kVisibilityDepthRatio);

/// `EquilibriumDiffusionAges` over the equilibrium lifetimes of `ages`
/// (`SteadyStateAges::EquilibriumLifetimeGyr`), matching the diffusion to them.
/// @throws std::invalid_argument as the lifetime overload, or for null `ages`.
DiffusionAgeModel EquilibriumDiffusionAges(std::shared_ptr<const DiffusedProfile> profile,
                                           FreshMorphometry fresh,
                                           std::shared_ptr<const SteadyStateAges> ages,
                                           double cutoffDepthRatio = kVisibilityDepthRatio);

/// Diffusion degradation that wears craters of the steady-state `ages` to the visibility cutoff at
/// the end of their lifetimes, on `profile` diffused from `fresh` (`EquilibriumDiffusionAges`).
/// @throws std::invalid_argument as `EquilibriumDiffusionAges` and `DiffusionDegradation`.
std::shared_ptr<const DiffusionDegradation>
MatchedDiffusionDegradation(std::shared_ptr<const DiffusedProfile> profile, FreshMorphometry fresh,
                            std::shared_ptr<const SteadyStateAges> ages);

}  // namespace terraingen
