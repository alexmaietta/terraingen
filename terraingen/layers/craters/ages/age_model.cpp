#include "terraingen/layers/craters/ages/age_model.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

void CheckDiameter(double diameter) {
    if (!IsPositive(diameter)) {
        throw std::domain_error("Diameter must be positive and finite");
    }
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

}  // namespace terraingen
