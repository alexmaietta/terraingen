#include "terraingen/layers/craters/ages/uniform.h"

#include "terraingen/util/checks.h"

#include <cmath>

namespace terraingen {

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
