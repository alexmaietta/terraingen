/// @file
/// @brief An in-place radix-2 fast Fourier transform.
#pragma once

#include <complex>
#include <vector>

namespace terraingen {

/// @brief Transforms values in place by the iterative radix-2 FFT, unnormalised:
/// X_k = sum_n x_n exp(`sign` 2 pi i n k / N).
/// @param values The values; their count must be a power of 2.
/// @param sign The sign of the exponent: -1 for the forward transform, 1 for the inverse.
void Fft(std::vector<std::complex<double>>& values, double sign);

}  // namespace terraingen
