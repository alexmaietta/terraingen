#include "terraingen/distributions/power_law.h"

#include "terraingen/distributions/checks.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

PowerLawSfd::PowerLawSfd(double coefficient, double exponent, SfdKind kind, DiameterRange range)
    : SizeFrequencyDistribution(kind, range), coefficient_(coefficient), exponent_(exponent) {
    if (!detail::PositiveFinite(coefficient_) || !detail::PositiveFinite(exponent_)) {
        throw std::invalid_argument(
            "Power law coefficient and exponent must be positive and finite");
    }
}

double PowerLawSfd::CumulativeAt(double diameter) const {
    return coefficient_ * std::pow(diameter, -exponent_);
}

double PowerLawSfd::IncrementalAt(double diameter) const {
    return coefficient_ * exponent_ * std::pow(diameter, -exponent_ - 1.0);
}

double PowerLawSfd::InvertCumulative(double target, double /*dMin*/, double /*dMax*/) const {
    return std::pow(target / coefficient_, -1.0 / exponent_);
}

double PowerLawCoefficientFromKilometres(double coefficient, double exponent) {
    return coefficient * std::pow(1000.0, exponent) / 1e6;
}

}  // namespace terraingen
