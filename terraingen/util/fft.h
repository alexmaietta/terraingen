/// @file
/// @brief In-place radix-2 fast Fourier transforms in one and two dimensions.
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace terraingen {

/// Transforms values in place by the iterative radix-2 FFT, unnormalized:
/// X_k = sum_n x_n exp(`sign` 2 pi i n k / N).
/// @param values The values; their count must be a power of 2.
/// @param sign The sign of the exponent: -1 for the forward transform, 1 for the inverse.
void Fft(std::vector<std::complex<double>>& values, double sign);

/// Transforms a `width` x `height` array stored row by row in place, unnormalized: `Fft` along
/// every row, then along every column.
/// @param width Columns; a power of 2.
/// @param height Rows; a power of 2.
/// @param sign As `Fft`.
/// @throws std::invalid_argument unless `values` holds `width` x `height` values.
void Fft2(std::vector<std::complex<double>>& values, std::size_t width, std::size_t height,
          double sign);

}  // namespace terraingen
