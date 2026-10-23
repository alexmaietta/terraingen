#include "terraingen/layers/craters/ages/freshness_ages.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <utility>

namespace terraingen {

namespace {

ValueDistribution ClampedToUnit(ValueDistribution freshness) {
    freshness.min = std::fmax(freshness.min, 0.0);
    freshness.max = std::fmin(freshness.max, 1.0);
    ValidateValueDistribution(freshness);
    return freshness;
}

}  // namespace

FreshnessAges::FreshnessAges(ValueDistribution freshness, double lifetimeGyr)
    : freshness_(ClampedToUnit(std::move(freshness))), lifetimeGyr_(lifetimeGyr) {
    RequirePositive(lifetimeGyr_, "Lifetime");
}

double FreshnessAges::SampleFreshness(RandomStream& random) const {
    return freshness_.Draw(random);
}

double FreshnessAges::LifetimeAt(double /*diameter*/) const {
    return lifetimeGyr_;
}

double FreshnessAges::SampleAgeAt(RandomStream& random, double /*diameter*/,
                                  double lifetimeGyr) const {
    return (1.0 - SampleFreshness(random)) * lifetimeGyr;
}

}  // namespace terraingen
