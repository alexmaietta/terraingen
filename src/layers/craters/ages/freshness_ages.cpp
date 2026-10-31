// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/ages/freshness_ages.h"

#include "util/checks.h"

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
    : m_freshness(ClampedToUnit(std::move(freshness))), m_lifetimeGyr(lifetimeGyr) {
    RequirePositive(m_lifetimeGyr, "Lifetime");
}

double FreshnessAges::SampleFreshness(RandomStream& random) const {
    return m_freshness.Draw(random);
}

double FreshnessAges::LifetimeAt(double /*diameter*/) const {
    return m_lifetimeGyr;
}

double FreshnessAges::SampleAgeAt(RandomStream& random, double /*diameter*/,
                                  double lifetimeGyr) const {
    return (1.0 - SampleFreshness(random)) * lifetimeGyr;
}

}  // namespace terraingen
