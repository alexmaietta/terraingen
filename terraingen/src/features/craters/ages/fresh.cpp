#include "terraingen/features/craters/ages/fresh.h"

namespace terraingen {

double FreshAges::LifetimeAt(double /*diameter*/) const {
    return 0.0;
}

double FreshAges::SampleAgeAt(RandomStream& /*random*/, double /*diameter*/,
                              double /*lifetimeGyr*/) const {
    return 0.0;
}

}  // namespace terraingen
