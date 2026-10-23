/// @file
/// @brief Lunar topographic diffusivities of Fassett and Thomson (2014) and Fassett et al. (2022).
#pragma once

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
/// equilibrium lifetime, as Fassett et al. (2022, section 4) derive the diffusivity that holds an
/// equilibrium: the fresh shape diffused to the cutoff, over the lifetime. A crater of diameter D
/// and age t gets kappa t = tau_cut(D) D^2 t / lifetime(D), so its shape follows its age model
/// whatever the profile.
/// @param fresh The fresh morphometry the degradation starts from.
/// @param lifetimeGyr The equilibrium lifetime in Gyr of craters of a diameter in meters, such as
/// `SteadyStateAges::EquilibriumLifetimeGyr`; positive.
/// @throws std::invalid_argument for a null `profile`, an empty `fresh` or `lifetimeGyr`, or a
/// cutoff outside (0, 1).
DiffusionAgeModel EquilibriumDiffusionAges(std::shared_ptr<const DiffusedProfile> profile,
                                           FreshMorphometry fresh,
                                           std::function<double(double)> lifetimeGyr,
                                           double cutoffDepthRatio = kVisibilityDepthRatio);

}  // namespace terraingen
