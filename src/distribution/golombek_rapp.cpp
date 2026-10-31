// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/golombek_rapp.h"

#include "terraingen/util/bits.h"
#include "terraingen/util/special_functions.h"
#include "util/checks.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

GolombekRappSfd::GolombekRappSfd(double coverage, double qa, double qb, DiameterRange range)
    : SizeFrequencyDistribution(SfdKind::Density, range), m_coverage(coverage) {
    if (!(m_coverage > 0.0 && m_coverage < 1.0)) {
        throw std::invalid_argument("Boulder coverage k must be between 0 and 1");
    }
    if (!(IsNonNegative(qa) && IsNonNegative(qb))) {
        throw std::invalid_argument("Golombek-Rapp qa and qb must be finite and not negative");
    }
    m_q = qa + qb / m_coverage;
    if (!(m_q > 0.0)) {
        throw std::invalid_argument("Golombek-Rapp q must be positive");
    }
}

double GolombekRappSfd::FractionalArea(double diameter) const {
    return m_coverage * std::exp(-m_q * diameter);
}

double GolombekRappSfd::CumulativeAt(double diameter) const {
    return 4.0 * m_coverage * m_q / kPi * ExponentialIntegral(2, m_q * diameter) / diameter;
}

double GolombekRappSfd::IncrementalAt(double diameter) const {
    return GolombekRappIncremental(diameter, m_coverage, m_q);
}

double GolombekRappIncremental(double diameter, double coverage, double q) {
    return coverage * q * std::exp(-q * diameter) / (kPi * diameter * diameter / 4.0);
}

}  // namespace terraingen
