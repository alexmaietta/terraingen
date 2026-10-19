/// @file
/// @brief `Bisect` and `BisectLog`, finding where a monotone condition turns true.
#pragma once

#include <cmath>

namespace terraingen {

/// The ends of an interval.
struct BisectionInterval {
    double low = 0.0;
    double high = 0.0;
};

/// Halves [`low`, `high`] at most `steps` times around where `above` turns true, keeping `above`
/// false at the low end and true at the high end, until the interval is no wider than
/// `tolerance`.
template <typename Above>
BisectionInterval Bisect(double low, double high, int steps, double tolerance, const Above& above) {
    for (int i = 0; i < steps && high - low > tolerance; ++i) {
        const double middle = 0.5 * (low + high);
        (above(middle) ? high : low) = middle;
    }
    return {low, high};
}

/// `Bisect` in ln x between positive `low` and `high`, `above` taking x; the high end found.
/// @param tolerance Width in ln x.
template <typename Above>
double BisectLog(double low, double high, int steps, double tolerance, const Above& above) {
    return std::exp(Bisect(std::log(low), std::log(high), steps, tolerance, [&](double middle) {
                        return above(std::exp(middle));
                    }).high);
}

}  // namespace terraingen
