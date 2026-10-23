// SPDX-License-Identifier: BSD-3-Clause

#include "support/statistics.h"
#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

using terraingen::RandomStream;
using terraingen::SamplePoisson;
using terraingen::testing::ChiSquareCritical;
using terraingen::testing::KolmogorovSmirnov;
using terraingen::testing::KolmogorovSmirnovCritical;

namespace {

constexpr std::size_t kSamples = 50000;

template <typename Draw> std::vector<double> Collect(Draw draw) {
    std::vector<double> samples;
    samples.reserve(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i) {
        samples.push_back(draw());
    }
    return samples;
}

double PoissonPmf(double mean, std::uint64_t k) {
    return std::exp(-mean + static_cast<double>(k) * std::log(mean) -
                    std::lgamma(static_cast<double>(k) + 1.0));
}

}  // namespace

TEST(RandomStatistics, UniformDoublesAreUniform) {
    RandomStream stream(1u, 100u);
    const auto samples = Collect([&] { return stream.NextDouble(); });
    EXPECT_LT(KolmogorovSmirnov(samples, [](double x) { return x; }),
              KolmogorovSmirnovCritical(kSamples));
}

TEST(RandomStatistics, PoissonMatchesItsMeanVarianceAndPmf) {
    for (const double mean : {0.5, 3.0, 9.9, 10.0, 25.0, 400.0}) {
        RandomStream stream(3u, static_cast<std::uint32_t>(mean * 10.0));
        std::vector<std::uint64_t> counts(kSamples);
        double sum = 0.0;
        for (auto& c : counts) {
            c = SamplePoisson(stream, mean);
            sum += static_cast<double>(c);
        }
        const double sampleMean = sum / kSamples;
        double variance = 0.0;
        for (const auto c : counts) {
            variance +=
                (static_cast<double>(c) - sampleMean) * (static_cast<double>(c) - sampleMean);
        }
        variance /= kSamples - 1;
        const double standardError = std::sqrt(mean / kSamples);
        EXPECT_NEAR(sampleMean, mean, 5.0 * standardError) << "mean " << mean;
        EXPECT_NEAR(variance / mean, 1.0, 0.05) << "mean " << mean;

        const auto lowest =
            static_cast<std::uint64_t>(std::fmax(0.0, mean - 4.0 * std::sqrt(mean)));
        const auto highest = static_cast<std::uint64_t>(mean + 4.0 * std::sqrt(mean)) + 1;
        double chiSquare = 0.0;
        int bins = 0;
        for (std::uint64_t k = lowest; k <= highest; ++k) {
            const double expected = PoissonPmf(mean, k) * kSamples;
            if (expected < 20.0) {
                continue;
            }
            double observed = 0.0;
            for (const auto c : counts) {
                observed += c == k ? 1.0 : 0.0;
            }
            chiSquare += (observed - expected) * (observed - expected) / expected;
            ++bins;
        }
        EXPECT_LT(chiSquare, ChiSquareCritical(bins)) << "mean " << mean;
    }
}

TEST(RandomStatistics, StandardNormalMatchesItsCdf) {
    RandomStream stream(6u, 100u);
    const auto samples = Collect([&] { return terraingen::StandardNormal(stream); });
    const auto cdf = [](double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); };
    EXPECT_LT(KolmogorovSmirnov(samples, cdf), KolmogorovSmirnovCritical(kSamples));
}

TEST(RandomStatistics, SubstreamsAreUncorrelated) {
    RandomStream a(8u, 1u, 0u, 0u, 0u);
    RandomStream b(8u, 1u, 1u, 0u, 0u);
    double sumA = 0.0;
    double sumB = 0.0;
    double sumAb = 0.0;
    double sumA2 = 0.0;
    double sumB2 = 0.0;
    for (std::size_t i = 0; i < kSamples; ++i) {
        const double x = a.NextDouble();
        const double y = b.NextDouble();
        sumA += x;
        sumB += y;
        sumAb += x * y;
        sumA2 += x * x;
        sumB2 += y * y;
    }
    const auto n = static_cast<double>(kSamples);
    const double covariance = sumAb / n - (sumA / n) * (sumB / n);
    const double correlation = covariance / std::sqrt((sumA2 / n - (sumA / n) * (sumA / n)) *
                                                      (sumB2 / n - (sumB / n) * (sumB / n)));
    EXPECT_LT(std::fabs(correlation), 4.0 / std::sqrt(n));
}
