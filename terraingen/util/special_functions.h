/// @file
/// @brief Special functions the models need: the exponential integral.
#pragma once

namespace terraingen {

/// @brief The exponential integral E_n(x) = integral from 1 to infinity of exp(-x t) / t^n dt.
///
/// Reference: W. H. Press, S. A. Teukolsky, W. T. Vetterling and B. P. Flannery, "Numerical
/// Recipes", 3rd ed., Cambridge University Press, 2007, section 6.3.
/// @param n The order; n >= 0.
/// @param x The argument; x > 0, or x >= 0 when n > 1.
/// @return E_n(x).
/// @throws std::domain_error outside that domain.
double ExponentialIntegral(int n, double x);

}  // namespace terraingen
