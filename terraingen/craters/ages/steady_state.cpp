#include "terraingen/craters/ages/steady_state.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

double SampleNeukumFormationAge(RandomStream& random, double maxAgeGyr) {
    const double target = random.NextDouble() * NeukumLunarChronology(maxAgeGyr);
    double low = 0.0;
    double high = maxAgeGyr;
    for (int i = 0; i < 100 && high - low > 1e-15 * maxAgeGyr; ++i) {
        const double middle = 0.5 * (low + high);
        if (NeukumLunarChronology(middle) < target) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return 0.5 * (low + high);
}

}  // namespace

SteadyStateAges::SteadyStateAges(std::shared_ptr<const SizeFrequencyDistribution> productionRate,
                                 std::shared_ptr<const SizeFrequencyDistribution> equilibrium,
                                 double ageGyr)
    : productionRate_(std::move(productionRate)), equilibrium_(std::move(equilibrium)),
      ageGyr_(ageGyr) {
    if (!productionRate_ || productionRate_->Kind() != SfdKind::ProductionRate) {
        throw std::invalid_argument("Steady-state ages need a production rate distribution");
    }
    if (!equilibrium_ || equilibrium_->Kind() != SfdKind::Density) {
        throw std::invalid_argument("Steady-state ages need an equilibrium density");
    }
    NeukumLunarChronology(ageGyr_);
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
    return SampleNeukumFormationAge(random, lifetimeGyr);
}

}  // namespace terraingen
