/// @file
/// @brief Special functions the models need: the exponential integral.
#pragma once

namespace terraingen {

/// The exponential integral E_n(x) = integral from 1 to infinity of exp(-x t) / t^n dt.
/// @param n The order; n >= 0.
/// @param x The argument; x > 0, or x >= 0 when n > 1.
/// @throws std::domain_error outside that domain.
double ExponentialIntegral(int n, double x);

}  // namespace terraingen
