#include "terraingen/distribution/golombek_rapp.h"

#include "terraingen/util/checks.h"
#include "terraingen/util/special_functions.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

GolombekRappSfd::GolombekRappSfd(double coverage, double qa, double qb, DiameterRange range)
    : SizeFrequencyDistribution(SfdKind::Density, range), coverage_(coverage) {
    if (!(coverage_ > 0.0 && coverage_ < 1.0)) {
        throw std::invalid_argument("Boulder coverage k must be between 0 and 1");
    }
    if (!(IsNonNegative(qa) && IsNonNegative(qb))) {
        throw std::invalid_argument("Golombek-Rapp qa and qb must be finite and not negative");
    }
    q_ = qa + qb / coverage_;
    if (!(q_ > 0.0)) {
        throw std::invalid_argument("Golombek-Rapp q must be positive");
    }
}

double GolombekRappSfd::FractionalArea(double diameter) const {
    return coverage_ * std::exp(-q_ * diameter);
}

double GolombekRappSfd::CumulativeAt(double diameter) const {
    return 4.0 * coverage_ * q_ / std::numbers::pi * ExponentialIntegral(2, q_ * diameter) /
           diameter;
}

double GolombekRappSfd::IncrementalAt(double diameter) const {
    return coverage_ * q_ * std::exp(-q_ * diameter) /
           (std::numbers::pi * diameter * diameter / 4.0);
}

}  // namespace terraingen
