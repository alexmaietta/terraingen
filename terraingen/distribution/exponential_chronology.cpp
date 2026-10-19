#include "terraingen/distribution/exponential_chronology.h"

#include <cmath>
#include <stdexcept>

namespace terraingen::detail {

namespace {

/// Square metres per square kilometre.
constexpr double kSquareKilometre = 1e6;

void CheckAge(const ExponentialChronology& chronology, double ageGyr) {
    if (!(ageGyr >= 0.0 && ageGyr <= chronology.maxAgeGyr)) {
        throw std::domain_error(chronology.ageError);
    }
}

}  // namespace

double ExponentialChronology::Count(double ageGyr) const {
    CheckAge(*this, ageGyr);
    return (burst * std::expm1(decay * ageGyr) + linear * ageGyr) / kSquareKilometre;
}

double ExponentialChronology::Rate(double ageGyr) const {
    CheckAge(*this, ageGyr);
    return (burst * decay * std::exp(decay * ageGyr) + linear) / kSquareKilometre;
}

}  // namespace terraingen::detail
