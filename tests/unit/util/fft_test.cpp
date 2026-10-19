#include "terraingen/util/fft.h"

#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
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
                sign * 2.0 * terraingen::kPi * static_cast<double>(j * k) / static_cast<double>(n);
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

TEST(Fft2, MatchesTransformsAlongRowsThenColumns) {
    constexpr std::size_t kWidth = 8;
    constexpr std::size_t kHeight = 4;
    std::vector<Complex> values;
    for (std::size_t i = 0; i < kWidth * kHeight; ++i) {
        values.emplace_back(std::sin(0.3 * static_cast<double>(i)), 0.05 * static_cast<double>(i));
    }
    std::vector<Complex> fast = values;
    terraingen::Fft2(fast, kWidth, kHeight, -1.0);
    for (std::size_t v = 0; v < kHeight; ++v) {
        for (std::size_t u = 0; u < kWidth; ++u) {
            Complex sum;
            for (std::size_t row = 0; row < kHeight; ++row) {
                for (std::size_t col = 0; col < kWidth; ++col) {
                    const double angle =
                        -2.0 * terraingen::kPi *
                        (static_cast<double>(u * col) / static_cast<double>(kWidth) +
                         static_cast<double>(v * row) / static_cast<double>(kHeight));
                    sum += values[row * kWidth + col] * Complex(std::cos(angle), std::sin(angle));
                }
            }
            EXPECT_NEAR(fast[v * kWidth + u].real(), sum.real(), 1e-12) << u << ", " << v;
            EXPECT_NEAR(fast[v * kWidth + u].imag(), sum.imag(), 1e-12) << u << ", " << v;
        }
    }
}

TEST(Fft2, RejectsAMismatchedSize) {
    std::vector<Complex> values(6);
    EXPECT_THROW(terraingen::Fft2(values, 4, 2, 1.0), std::invalid_argument);
}
