#include "terraingen/layers/craters/ages/uniform.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

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

}  // namespace terraingen
