// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/log_polynomial.h"

#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

LogPolynomialSfd::LogPolynomialSfd(std::vector<double> coefficients, SfdKind kind,
                                   DiameterRange range, double diameterUnit, double areaUnit)
    : SizeFrequencyDistribution(kind, range), m_coefficients(std::move(coefficients)),
      m_diameterUnit(diameterUnit), m_areaUnit(areaUnit) {
    if (m_coefficients.empty() || !std::all_of(m_coefficients.begin(), m_coefficients.end(),
                                               [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument("A log polynomial needs finite coefficients");
    }
    if (!IsPositive(m_diameterUnit) || !IsPositive(m_areaUnit)) {
        throw std::invalid_argument("Diameter and area units must be positive and finite");
    }
    if (!(range.min > 0.0 && std::isfinite(range.max))) {
        throw std::invalid_argument(
            "A log polynomial needs a finite range with a positive minimum");
    }
    constexpr int kSamples = 4096;
    const double low = std::log10(range.min / m_diameterUnit);
    const double high = std::log10(range.max / m_diameterUnit);
    for (int i = 0; i <= kSamples; ++i) {
        if (Derivative(low + (high - low) * i / kSamples) > 0.0) {
            throw std::invalid_argument("A cumulative distribution cannot increase with diameter");
        }
    }
}

double LogPolynomialSfd::Polynomial(double x) const {
    double sum = 0.0;
    for (auto it = m_coefficients.rbegin(); it != m_coefficients.rend(); ++it) {
        sum = sum * x + *it;
    }
    return sum;
}

double LogPolynomialSfd::Derivative(double x) const {
    double sum = 0.0;
    for (std::size_t i = m_coefficients.size() - 1; i > 0; --i) {
        sum = sum * x + static_cast<double>(i) * m_coefficients[i];
    }
    return sum;
}

double LogPolynomialSfd::CumulativeAt(double diameter) const {
    return std::pow(10.0, Polynomial(std::log10(diameter / m_diameterUnit))) / m_areaUnit;
}

double LogPolynomialSfd::IncrementalAt(double diameter) const {
    return -CumulativeAt(diameter) * Derivative(std::log10(diameter / m_diameterUnit)) / diameter;
}

}  // namespace terraingen
