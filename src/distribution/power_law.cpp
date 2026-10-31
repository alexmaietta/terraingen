// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/power_law.h"

#include "util/checks.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

PowerLawSfd::PowerLawSfd(double coefficient, double exponent, SfdKind kind, DiameterRange range)
    : SizeFrequencyDistribution(kind, range), law_{range.min, coefficient, exponent} {
    if (!IsPositive(coefficient) || !IsPositive(exponent)) {
        throw std::invalid_argument(
            "Power law coefficient and exponent must be positive and finite");
    }
}

double PowerLawSfd::CumulativeAt(double diameter) const {
    return law_.Cumulative(diameter);
}

double PowerLawSfd::IncrementalAt(double diameter) const {
    return law_.Incremental(diameter);
}

double PowerLawSfd::InvertCumulative(double target, double /*minDiameter*/,
                                     double /*maxDiameter*/) const {
    return law_.Invert(target);
}

double PowerLawCoefficientFromKilometers(double coefficient, double exponent) {
    return coefficient * std::pow(1000.0, exponent) / 1e6;
}

}  // namespace terraingen
