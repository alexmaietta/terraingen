// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The lunar crater density and ages that tests generate craters from: an extended Neukum
/// production surface capped by Trask equilibrium, with steady-state ages.
#pragma once

#include "terraingen/distribution/equilibrium.h"
#include "terraingen/distribution/minimum.h"
#include "terraingen/distribution/neukum.h"
#include "terraingen/distribution/production_surface.h"
#include "terraingen/layers/craters/ages/steady_state.h"

#include <memory>
#include <vector>

namespace terraingen::testing {

/// @brief The extended Neukum production function, shared.
/// @return A `ProductionRate` distribution from 2.5 m to 300 km.
inline std::shared_ptr<const SizeFrequencyDistribution> LunarProduction() {
    return std::make_shared<SplicedSfd>(ExtendedNeukumProductionFunction());
}

/// @brief Trask equilibrium, shared.
/// @return A `Density` distribution.
inline std::shared_ptr<const SizeFrequencyDistribution> LunarEquilibrium() {
    return std::make_shared<PowerLawSfd>(TraskEquilibrium());
}

/// @brief The crater density of a lunar surface: the least of its extended Neukum production
/// surface and Trask equilibrium.
/// @param ageGyr The surface age in Gyr.
/// @return Craters per m^2.
/// @throws std::domain_error if `NeukumLunarChronology` rejects `ageGyr`.
inline std::shared_ptr<const SizeFrequencyDistribution> LunarCraterDensity(double ageGyr) {
    return std::make_shared<MinimumSfd>(
        std::vector<std::shared_ptr<const SizeFrequencyDistribution>>{
            std::make_shared<ProductionSurface>(LunarProduction(), ageGyr, NeukumLunarChronology),
            LunarEquilibrium()});
}

/// @brief The crater ages of a lunar surface: steady-state ages from the extended Neukum
/// production function and Trask equilibrium.
/// @param ageGyr The surface age in Gyr.
/// @return The age model.
/// @throws std::domain_error if `NeukumLunarChronology` rejects `ageGyr`.
inline std::shared_ptr<const SteadyStateAges> LunarCraterAges(double ageGyr) {
    return std::make_shared<SteadyStateAges>(LunarProduction(), LunarEquilibrium(), ageGyr,
                                             NeukumLunarChronology);
}

}  // namespace terraingen::testing
