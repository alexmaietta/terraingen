#include "terraingen/util/fft.h"

#include "terraingen/util/bits.h"

#include <cmath>
#include <cstddef>
#include <utility>

namespace terraingen {

namespace {

/// Reorders `values` by bit-reversed index, the input order of the iterative transform.
void BitReverse(std::vector<std::complex<double>>& values) {
    const std::size_t n = values.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(values[i], values[j]);
        }
    }
}

}  // namespace

void Fft(std::vector<std::complex<double>>& values, double sign) {
    BitReverse(values);
    const std::size_t n = values.size();
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const double angle = sign * 2.0 * kPi / static_cast<double>(length);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < n; start += length) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < length / 2; ++k) {
                const std::complex<double> even = values[start + k];
                const std::complex<double> odd = values[start + k + length / 2] * w;
                values[start + k] = even + odd;
                values[start + k + length / 2] = even - odd;
                w *= step;
            }
        }
    }
}

}  // namespace terraingen
