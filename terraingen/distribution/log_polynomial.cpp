#include "terraingen/distribution/log_polynomial.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

LogPolynomialSfd::LogPolynomialSfd(std::vector<double> coefficients, DiameterRange range,
                                   SfdKind kind, double diameterUnit, double areaUnit)
    : SizeFrequencyDistribution(kind, range), coefficients_(std::move(coefficients)),
      diameterUnit_(diameterUnit), areaUnit_(areaUnit) {
    if (coefficients_.empty() || !std::all_of(coefficients_.begin(), coefficients_.end(),
                                              [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument("A log polynomial needs finite coefficients");
    }
    if (!IsPositive(diameterUnit_) || !IsPositive(areaUnit_)) {
        throw std::invalid_argument("Diameter and area units must be positive and finite");
    }
    if (!(range.min > 0.0 && std::isfinite(range.max))) {
        throw std::invalid_argument(
            "A log polynomial needs a finite range with a positive minimum");
    }
    constexpr int kSamples = 4096;
    const double low = std::log10(range.min / diameterUnit_);
    const double high = std::log10(range.max / diameterUnit_);
    for (int i = 0; i <= kSamples; ++i) {
        if (Derivative(low + (high - low) * i / kSamples) > 0.0) {
            throw std::invalid_argument("A cumulative distribution cannot increase with diameter");
        }
    }
}

double LogPolynomialSfd::Polynomial(double x) const {
    double sum = 0.0;
    for (auto it = coefficients_.rbegin(); it != coefficients_.rend(); ++it) {
        sum = sum * x + *it;
    }
    return sum;
}

double LogPolynomialSfd::Derivative(double x) const {
    double sum = 0.0;
    for (std::size_t i = coefficients_.size() - 1; i > 0; --i) {
        sum = sum * x + static_cast<double>(i) * coefficients_[i];
    }
    return sum;
}

double LogPolynomialSfd::CumulativeAt(double diameter) const {
    return std::pow(10.0, Polynomial(std::log10(diameter / diameterUnit_))) / areaUnit_;
}

double LogPolynomialSfd::IncrementalAt(double diameter) const {
    return -CumulativeAt(diameter) * Derivative(std::log10(diameter / diameterUnit_)) / diameter;
}

}  // namespace terraingen
