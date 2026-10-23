// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/degradation/diffusivities.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

PowerLawDiffusivity FassettThomson2014Diffusivity() {
    return {5.5, 0.0, 0.0};
}

PowerLawDiffusivity Fassett2022HartmannDiffusivity() {
    return {3.6, 0.87, 5.0};
}

PowerLawDiffusivity Fassett2022TraskDiffusivity() {
    return {3.1, 1.05, 5.0};
}

double FassettThomson2014DiffusionAge(double ageGyr) {
    const double t = ageGyr;
    const double kappaT =
        (((435.83 * t - 3621.2) * t + 11204.0) * t - 16811.0) * t * t + 17546.0 * t;
    return std::fmax(0.0, kappaT);
}

DiffusionAgeModel EquilibriumDiffusionAges(std::shared_ptr<const DiffusedProfile> profile,
                                           FreshMorphometry fresh,
                                           std::function<double(double)> lifetimeGyr,
                                           double cutoffDepthRatio) {
    if (!profile || !fresh || !lifetimeGyr) {
        throw std::invalid_argument(
            "Equilibrium diffusion ages need a diffused profile, fresh morphometry and lifetimes");
    }
    if (!(cutoffDepthRatio > 0.0 && cutoffDepthRatio < 1.0)) {
        throw std::invalid_argument("A visibility depth ratio must be in (0, 1)");
    }
    return [profile = std::move(profile), fresh = std::move(fresh),
            lifetimeGyr = std::move(lifetimeGyr),
            cutoffDepthRatio](double diameter, double ageGyr) {
        const double cut = profile->AgeAtDepthRatio(fresh(diameter), diameter, cutoffDepthRatio);
        return cut * diameter * diameter * ageGyr / lifetimeGyr(diameter);
    };
}

}  // namespace terraingen
