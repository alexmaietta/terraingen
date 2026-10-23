// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ForEachLogSimpsonNode`, Simpson's rule in log x.
#pragma once

#include <cmath>

namespace terraingen {

/// Calls `visit(x, weight)` at the nodes of Simpson's rule in ln x over [`low`, `high`], so the
/// sum of weight f(x) approximates the integral of f(x) dx.
/// @param low Positive, below `high`.
/// @param intervals Even and positive.
template <typename Visit>
void ForEachLogSimpsonNode(double low, double high, int intervals, const Visit& visit) {
    const double a = std::log(low);
    const double h = (std::log(high) - a) / intervals;
    for (int i = 0; i <= intervals; ++i) {
        const double x = std::exp(a + h * i);
        const double simpson = (i == 0 || i == intervals) ? 1.0 : (i % 2 == 1 ? 4.0 : 2.0);
        visit(x, simpson * h / 3.0 * x);
    }
}

}  // namespace terraingen
