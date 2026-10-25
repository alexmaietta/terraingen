// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/ages/steady_state.h"

#include "terraingen/distribution/minimum.h"
#include "util/bisection.h"

#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// An age in [0, `maxAgeGyr`] drawn from the formation rate of `chronology`, by bisection on
/// N1(T).
double SampleFormationAge(RandomStream& random, const Chronology& chronology, double maxAgeGyr) {
    const double target = random.NextDouble() * chronology(maxAgeGyr);
    const BisectionInterval age = BisectRising(0.0, maxAgeGyr, 100, 1e-15 * maxAgeGyr, target,
                                               [&](double t) { return chronology(t); });
    return 0.5 * (age.low + age.high);
}

}  // namespace

SteadyStateAges::SteadyStateAges(std::shared_ptr<const SizeFrequencyDistribution> productionRate,
                                 std::shared_ptr<const SizeFrequencyDistribution> equilibrium,
                                 double ageGyr, Chronology chronology)
    : productionRate_(std::move(productionRate)), equilibrium_(std::move(equilibrium)),
      ageGyr_(ageGyr), chronology_(std::move(chronology)) {
    if (!productionRate_ || productionRate_->Kind() != SfdKind::ProductionRate) {
        throw std::invalid_argument("Steady-state ages need a production rate distribution");
    }
    if (!equilibrium_ || equilibrium_->Kind() != SfdKind::Density) {
        throw std::invalid_argument("Steady-state ages need an equilibrium density");
    }
    if (!chronology_) {
        throw std::invalid_argument("Steady-state ages need a chronology");
    }
    chronology_(ageGyr_);
}

double SteadyStateAges::EquilibriumLifetimeGyr(double diameter) const {
    return equilibrium_->Incremental(diameter) / productionRate_->Incremental(diameter);
}

double SteadyStateAges::LifetimeAt(double diameter) const {
    return std::fmin(EquilibriumLifetimeGyr(diameter), ageGyr_);
}

double SteadyStateAges::SampleAgeAt(RandomStream& random, double /*diameter*/,
                                    double lifetimeGyr) const {
    if (lifetimeGyr == 0.0) {
        return 0.0;
    }
    return SampleFormationAge(random, chronology_, lifetimeGyr);
}

std::shared_ptr<const SizeFrequencyDistribution> EquilibriumSurface::Density() const {
    return std::make_shared<MinimumSfd>(
        std::vector<std::shared_ptr<const SizeFrequencyDistribution>>{
            std::make_shared<ProductionSurface>(production, ageGyr, chronology), equilibrium});
}

std::shared_ptr<const SteadyStateAges> EquilibriumSurface::Ages() const {
    return std::make_shared<SteadyStateAges>(production, equilibrium, ageGyr, chronology);
}

}  // namespace terraingen
