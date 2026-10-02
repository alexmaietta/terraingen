#include "support/statistics.h"
#include "terraingen/models/size_frequency.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using terraingen::ExtendedNeukumProductionFunction;
using terraingen::LogPolynomial;
using terraingen::MooreDistribution;
using terraingen::MooreTerrain;
using terraingen::NeukumProductionFunction;
using terraingen::PowerLaw;
using terraingen::ProductionSurface;
using terraingen::RandomStream;
using terraingen::SizeFrequencyDistribution;
using terraingen::SplicedSfd;
using terraingen::TabulatedSfd;
using terraingen::ViperSpecification;
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

TEST(SfdStatistics, MooreSmoothMareSamplesMatchTheirCdfAcrossAllSegments) {
    const auto smooth = MooreDistribution(MooreTerrain::SmoothMare);
    const auto samples = Sample(smooth, 4u, 10.0, 1000.0);
    EXPECT_LT(
        KolmogorovSmirnov(samples, [&](double d) { return TruncatedCdf(smooth, 10.0, 1000.0, d); }),
        KolmogorovSmirnovCritical(kSamples));
}

TEST(SfdStatistics, NeukumSurfaceSamplesMatchTheirCdfOverTheFullRange) {
    const ProductionSurface surface(std::make_shared<LogPolynomial>(NeukumProductionFunction()),
                                    3.5);
    const auto samples = Sample(surface, 5u, 10.0, 300000.0);
    EXPECT_LT(KolmogorovSmirnov(samples,
                                [&](double d) { return TruncatedCdf(surface, 10.0, 300000.0, d); }),
              KolmogorovSmirnovCritical(kSamples));
}

TEST(SfdStatistics, ExtendedNeukumSurfaceSamplesMatchTheirCdfAcrossTheSplice) {
    const ProductionSurface surface(
        std::make_shared<SplicedSfd>(ExtendedNeukumProductionFunction()), 3.5);
    const auto samples = Sample(surface, 6u, 2.5, 1000.0);
    EXPECT_LT(
        KolmogorovSmirnov(samples, [&](double d) { return TruncatedCdf(surface, 2.5, 1000.0, d); }),
        KolmogorovSmirnovCritical(kSamples));
}

TEST(SfdStatistics, TabulatedSamplesMatchTheirCdf) {
    const TabulatedSfd table({1.0, 3.0, 10.0, 40.0, 100.0}, {1.0, 0.2, 0.01, 1e-3, 2e-5});
    const auto samples = Sample(table, 7u, 1.0, 100.0);
    EXPECT_LT(
        KolmogorovSmirnov(samples, [&](double d) { return TruncatedCdf(table, 1.0, 100.0, d); }),
        KolmogorovSmirnovCritical(kSamples));
}

TEST(SfdStatistics, ViperDiscontinuityBecomesAnAtomAtTheJoin) {
    const auto viper = ViperSpecification();
    const auto samples = Sample(viper, 5u, 20.0, 400.0);
    const double atJoin =
        static_cast<double>(std::count(samples.begin(), samples.end(), 80.0)) / kSamples;
    const double expected = (viper.Cumulative(80.0) - viper.Cumulative(80.0 * (1.0 + 1e-12))) /
                            (viper.Cumulative(20.0) - viper.Cumulative(400.0));
    const double sigma = std::sqrt(expected * (1.0 - expected) / kSamples);
    EXPECT_GT(expected, 0.01);
    EXPECT_NEAR(atJoin, expected, 5.0 * sigma);
}
