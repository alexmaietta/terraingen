#include "support/statistics.h"
#include "terraingen/distributions/size_frequency.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using terraingen::PowerLaw;
using terraingen::RandomStream;
using terraingen::SizeFrequencyDistribution;
using terraingen::TabulatedSfd;
using terraingen::testing::KolmogorovSmirnov;
using terraingen::testing::KolmogorovSmirnovCritical;
using terraingen::testing::PowerLawExponentMle;

namespace {

constexpr std::size_t kSamples = 40000;

std::vector<double> Sample(const SizeFrequencyDistribution& sfd, std::uint32_t seed, double dMin,
                           double dMax) {
    RandomStream stream(seed, 77u);
    std::vector<double> samples(kSamples);
    for (double& d : samples) {
        d = sfd.SampleDiameter(stream, dMin, dMax);
    }
    return samples;
}

double TruncatedCdf(const SizeFrequencyDistribution& sfd, double dMin, double dMax, double d) {
    const double top = sfd.Cumulative(dMin);
    return (top - sfd.Cumulative(d)) / (top - sfd.Cumulative(dMax));
}

}  // namespace

TEST(SfdStatistics, MaximumLikelihoodRecoversThePowerLawExponentWithBootstrapInterval) {
    const PowerLaw law(1.0, 2.7);
    const auto samples = Sample(law, 1u, 1.0, std::numeric_limits<double>::infinity());
    const double estimate = PowerLawExponentMle(samples, 1.0);
    EXPECT_NEAR(estimate, 2.7, 4.0 * 2.7 / std::sqrt(static_cast<double>(kSamples)));

    RandomStream resampler(2u, 2u);
    std::vector<double> estimates;
    std::vector<double> resample(kSamples);
    for (int b = 0; b < 400; ++b) {
        for (double& d : resample) {
            d = samples[static_cast<std::size_t>(resampler.NextDouble() * kSamples)];
        }
        estimates.push_back(PowerLawExponentMle(resample, 1.0));
    }
    std::sort(estimates.begin(), estimates.end());
    EXPECT_LE(estimates[2], 2.7);
    EXPECT_GE(estimates[397], 2.7);
}

TEST(SfdStatistics, TruncatedPowerLawSamplesMatchTheirCdf) {
    const PowerLaw law(1.0, 1.9);
    const auto samples = Sample(law, 3u, 1.0, 100.0);
    EXPECT_LT(
        KolmogorovSmirnov(samples, [&](double d) { return TruncatedCdf(law, 1.0, 100.0, d); }),
        KolmogorovSmirnovCritical(kSamples));
}

TEST(SfdStatistics, TabulatedSamplesMatchTheirCdf) {
    const TabulatedSfd table({1.0, 3.0, 10.0, 40.0, 100.0}, {1.0, 0.2, 0.01, 1e-3, 2e-5});
    const auto samples = Sample(table, 7u, 1.0, 100.0);
    EXPECT_LT(
        KolmogorovSmirnov(samples, [&](double d) { return TruncatedCdf(table, 1.0, 100.0, d); }),
        KolmogorovSmirnovCritical(kSamples));
}
