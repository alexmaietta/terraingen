#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace terraingen::testing {

/// Kolmogorov–Smirnov statistic D between `samples` and the continuous CDF `cdf`.
template <typename Cdf> double KolmogorovSmirnov(std::vector<double> samples, Cdf cdf) {
    std::sort(samples.begin(), samples.end());
    const auto n = static_cast<double>(samples.size());
    double d = 0.0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double f = cdf(samples[i]);
        d = std::max({d, f - static_cast<double>(i) / n, static_cast<double>(i + 1) / n - f});
    }
    return d;
}

/// Kolmogorov–Smirnov critical value at significance 0.001 for `n` samples (large-n asymptote).
inline double KolmogorovSmirnovCritical(std::size_t n) {
    return 1.949 / std::sqrt(static_cast<double>(n));
}

/// Chi-square critical value at significance 0.001 for `degrees` degrees of freedom
/// (Wilson–Hilferty approximation).
inline double ChiSquareCritical(double degrees) {
    const double z = 3.0902;
    const double t = 2.0 / (9.0 * degrees);
    return degrees * std::pow(1.0 - t + z * std::sqrt(t), 3.0);
}

}  // namespace terraingen::testing
