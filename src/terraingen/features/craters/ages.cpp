#include "terraingen/features/craters/ages.h"

#include "terraingen/distributions/composite.h"
#include "terraingen/features/craters/lunar/equilibrium.h"
#include "terraingen/features/craters/lunar/production.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

void CheckDiameter(double diameter) {
    if (!(std::isfinite(diameter) && diameter > 0.0)) {
        throw std::domain_error("Diameter must be positive and finite");
    }
}

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

double CraterAgeModel::LifetimeGyr(double diameter) const {
    CheckDiameter(diameter);
    return LifetimeAt(diameter);
}

double CraterAgeModel::SampleAgeGyr(RandomStream& random, double diameter) const {
    const double lifetime = LifetimeGyr(diameter);
    return std::clamp(SampleAgeAt(random, diameter, lifetime), 0.0, lifetime);
}

double CraterAgeModel::Freshness(double diameter, double ageGyr) const {
    const double lifetime = LifetimeGyr(diameter);
    if (lifetime == 0.0) {
        return 1.0;
    }
    return std::clamp(1.0 - ageGyr / lifetime, 0.0, 1.0);
}

double FreshAges::LifetimeAt(double /*diameter*/) const {
    return 0.0;
}

double FreshAges::SampleAgeAt(RandomStream& /*random*/, double /*diameter*/,
                              double /*lifetimeGyr*/) const {
    return 0.0;
}

UniformAges::UniformAges(double lifetimeGyr) : lifetimeGyr_(lifetimeGyr) {
    if (!(std::isfinite(lifetimeGyr_) && lifetimeGyr_ > 0.0)) {
        throw std::invalid_argument("Lifetime must be positive and finite");
    }
}

double UniformAges::LifetimeAt(double /*diameter*/) const {
    return lifetimeGyr_;
}

double UniformAges::SampleAgeAt(RandomStream& random, double /*diameter*/,
                                double lifetimeGyr) const {
    return random.NextDouble() * lifetimeGyr;
}

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

SurfaceModel LunarSurface(double ageGyr) {
    auto production = std::make_shared<SplicedSfd>(ExtendedNeukumProductionFunction());
    auto equilibrium = std::make_shared<PowerLawSfd>(TraskEquilibrium());
    auto surface = std::make_shared<ProductionSurface>(production, ageGyr);
    return SurfaceModel{
        std::make_shared<MinimumSfd>(
            std::vector<std::shared_ptr<const SizeFrequencyDistribution>>{surface, equilibrium}),
        std::make_shared<SteadyStateAges>(production, equilibrium, ageGyr)};
}

}  // namespace terraingen
