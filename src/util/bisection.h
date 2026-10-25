// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `Bisect` and `BisectLog`, finding where a monotone condition turns true.
#pragma once

#include <cmath>
#include <initializer_list>
#include <limits>

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

/// `Bisect` of `above(x) = !(value(x) < target)` for a non-decreasing `value`, to the same
/// interval, with fewer calls of `value` when it is smooth (about 22 rather than 51 for a lunar
/// chronology at a tolerance of 1e-15 of the interval): up to 12 steps of the Illinois method
/// estimate the crossing, points either side of it are checked to bracket it, and the halving is
/// replayed, calling `value` only at middles near the bracket.
/// @param value Callable as `double(double x)`; non-decreasing over [`low`, `high`], or the
/// interval may differ from `Bisect`'s.
template <typename Value>
BisectionInterval BisectRising(double low, double high, int steps, double tolerance, double target,
                               const Value& value) {
    constexpr int kEstimateSteps = 12;
    const double margin = 1e-13 * (high - low);
    double a = low;
    double b = high;
    double fa = value(a) - target;
    double fb = value(b) - target;
    bool belowKnown = fa < 0.0;
    bool aboveKnown = !(fb < 0.0);
    double below = a;
    double above = b;
    if (belowKnown && aboveKnown) {
        int side = 0;
        double c = a;
        for (int i = 0; i < kEstimateSteps; ++i) {
            const double previous = c;
            c = b - fb * (b - a) / (fb - fa);
            if (!(c > a && c < b)) {
                c = 0.5 * (a + b);
            }
            const double fc = value(c) - target;
            if (fc < 0.0) {
                a = c;
                fa = fc;
                if (side == -1) {
                    fb *= 0.5;
                }
                side = -1;
            } else {
                b = c;
                fb = fc;
                if (side == 1) {
                    fa *= 0.5;
                }
                side = 1;
            }
            if (i > 0 && std::fabs(c - previous) <= margin) {
                break;
            }
        }
        below = a;
        above = b;
        for (const double spread : {margin, 64.0 * margin}) {
            if (b - a <= 2.0 * spread) {
                break;
            }
            if (c - spread > a && value(c - spread) < target) {
                below = c - spread;
            }
            if (c + spread < b && !(value(c + spread) < target)) {
                above = c + spread;
            }
            if (above - below <= 4.0 * spread) {
                break;
            }
        }
    }
    const double aboveFrom = aboveKnown ? above + margin : std::numeric_limits<double>::infinity();
    const double belowTo = belowKnown ? below - margin : -std::numeric_limits<double>::infinity();
    for (int i = 0; i < steps && high - low > tolerance; ++i) {
        const double middle = 0.5 * (low + high);
        bool isAbove = middle >= aboveFrom;
        if (!isAbove && !(middle <= belowTo)) {
            isAbove = !(value(middle) < target);
        }
        high = isAbove ? middle : high;
        low = isAbove ? low : middle;
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
