// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/ages/age_model.h"

#include "util/checks.h"

#include <algorithm>

namespace terraingen {

double CraterAgeModel::LifetimeGyr(double diameter) const {
    RequireDiameter(diameter);
    return LifetimeAt(diameter);
}

double CraterAgeModel::SampleAgeGyr(RandomStream& random, double diameter) const {
    const double lifetime = LifetimeGyr(diameter);
    return std::clamp(SampleAgeAt(random, diameter, lifetime), 0.0, lifetime);
}

double CraterAgeModel::RemainingLifetime(double diameter, double ageGyr) const {
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
    RequirePositive(lifetimeGyr_, "Lifetime");
}

double UniformAges::LifetimeAt(double /*diameter*/) const {
    return lifetimeGyr_;
}

double UniformAges::SampleAgeAt(RandomStream& random, double /*diameter*/,
                                double lifetimeGyr) const {
    return random.NextDouble() * lifetimeGyr;
}

}  // namespace terraingen
