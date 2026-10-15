#include "terraingen/util/fft.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace {

using Complex = std::complex<double>;

/// The transform by its definition, X_k = sum_n x_n exp(sign 2 pi i n k / N).
std::vector<Complex> Direct(const std::vector<Complex>& values, double sign) {
    const std::size_t n = values.size();
    std::vector<Complex> result(n);
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t j = 0; j < n; ++j) {
            const double angle =
                sign * 2.0 * std::numbers::pi * static_cast<double>(j * k) / static_cast<double>(n);
            result[k] += values[j] * Complex(std::cos(angle), std::sin(angle));
        }
    }
    return result;
}

}  // namespace

TEST(Fft, MatchesTheDefinitionInBothDirections) {
    std::vector<Complex> values;
    for (int i = 0; i < 16; ++i) {
        values.emplace_back(std::sin(0.7 * i) + 0.1 * i, std::cos(1.3 * i));
    }
    for (const double sign : {-1.0, 1.0}) {
        std::vector<Complex> fast = values;
        terraingen::Fft(fast, sign);
        const std::vector<Complex> slow = Direct(values, sign);
        for (std::size_t k = 0; k < values.size(); ++k) {
            EXPECT_NEAR(fast[k].real(), slow[k].real(), 1e-12) << k;
            EXPECT_NEAR(fast[k].imag(), slow[k].imag(), 1e-12) << k;
        }
    }
}

TEST(Fft, LeavesASingleValueAlone) {
    std::vector<Complex> one{{2.5, -1.0}};
    terraingen::Fft(one, 1.0);
    EXPECT_EQ(one[0], Complex(2.5, -1.0));
}
