// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Bisection for where a monotone condition turns true, and Simpson's rule in log x.
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

/// A bracket [`a`, `b`] of a root of a continuous function, narrowed by the Illinois variant of
/// regula falsi; `fa` and `fb` are the function's values at the ends, of opposite signs.
struct IllinoisBracket {
    double a = 0.0;
    double b = 0.0;
    double fa = 0.0;
    double fb = 0.0;
    int side = 0;

    /// The next estimate of the root: the secant's zero, or the middle where it leaves the bracket.
    double Next() const {
        const double c = b - fb * (b - a) / (fb - fa);
        return c > a && c < b ? c : 0.5 * (a + b);
    }

    /// Moves the end on the side of `c` to it, given its value `fc`, halving the other end's value
    /// when the same end moved last time.
    void Update(double c, double fc) {
        if ((fc < 0.0) == (fa < 0.0)) {
            a = c;
            fa = fc;
            fb *= side == -1 ? 0.5 : 1.0;
            side = -1;
        } else {
            b = c;
            fb = fc;
            fa *= side == 1 ? 0.5 : 1.0;
            side = 1;
        }
    }
};

/// `Bisect` of `above(x) = !(value(x) < target)` for a non-decreasing `value`, to the same
/// interval with fewer calls of `value`, which Illinois steps bracket near the crossing.
/// @param value Callable as `double(double x)`; non-decreasing over [`low`, `high`], or the
/// interval may differ from `Bisect`'s.
template <typename Value>
BisectionInterval BisectRising(double low, double high, int steps, double tolerance, double target,
                               const Value& value) {
    constexpr int kEstimateSteps = 12;
    const double margin = 1e-13 * (high - low);
    IllinoisBracket bracket{low, high, value(low) - target, value(high) - target};
    const bool belowKnown = bracket.fa < 0.0;
    const bool aboveKnown = !(bracket.fb < 0.0);
    double below = low;
    double above = high;
    if (belowKnown && aboveKnown) {
        double c = low;
        for (int i = 0; i < kEstimateSteps; ++i) {
            const double previous = c;
            c = bracket.Next();
            bracket.Update(c, value(c) - target);
            if (i > 0 && std::fabs(c - previous) <= margin) {
                break;
            }
        }
        const double a = bracket.a;
        const double b = bracket.b;
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

/// Calls `visit(x, weight)` at the nodes of Simpson's rule in ln x over [`low`, `high`], so the
/// sum of weight f(x) approximates the integral of f(x) dx; the end nodes are `low` and `high`
/// exactly.
/// @param low Positive, below `high`.
/// @param intervals Even and positive.
template <typename Visit>
void ForEachLogSimpsonNode(double low, double high, int intervals, const Visit& visit) {
    const double a = std::log(low);
    const double h = (std::log(high) - a) / intervals;
    for (int i = 0; i <= intervals; ++i) {
        const double x = i == 0 ? low : (i == intervals ? high : std::exp(a + h * i));
        const double simpson = (i == 0 || i == intervals) ? 1.0 : (i % 2 == 1 ? 4.0 : 2.0);
        visit(x, simpson * h / 3.0 * x);
    }
}

}  // namespace terraingen
