#include "support/statistics.h"
#include "terraingen/features/craters/lunar_cratering.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

using terraingen::ExtendedNeukumProductionFunction;
using terraingen::LogPolynomial;
using terraingen::MooreDistribution;
using terraingen::MooreTerrain;
using terraingen::NeukumProductionFunction;
using terraingen::ProductionSurface;
using terraingen::RandomStream;
using terraingen::SizeFrequencyDistribution;
using terraingen::SplicedSfd;
using terraingen::ViperSpecification;
using terraingen::testing::KolmogorovSmirnov;
using terraingen::testing::KolmogorovSmirnovCritical;

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

TEST(LunarCrateringStatistics, NeukumSurfaceSamplesMatchTheirCdfOverTheFullRange) {
    const ProductionSurface surface(std::make_shared<LogPolynomial>(NeukumProductionFunction()),
                                    3.5);
    const auto samples = Sample(surface, 5u, 10.0, 300000.0);
    EXPECT_LT(KolmogorovSmirnov(samples,
                                [&](double d) { return TruncatedCdf(surface, 10.0, 300000.0, d); }),
              KolmogorovSmirnovCritical(kSamples));
}

TEST(LunarCrateringStatistics, ExtendedNeukumSurfaceSamplesMatchTheirCdfAcrossTheSplice) {
    const ProductionSurface surface(
        std::make_shared<SplicedSfd>(ExtendedNeukumProductionFunction()), 3.5);
    const auto samples = Sample(surface, 6u, 2.5, 1000.0);
    EXPECT_LT(
        KolmogorovSmirnov(samples, [&](double d) { return TruncatedCdf(surface, 2.5, 1000.0, d); }),
        KolmogorovSmirnovCritical(kSamples));
}

TEST(LunarCrateringStatistics, MooreSmoothMareSamplesMatchTheirCdfAcrossAllSegments) {
    const auto smooth = MooreDistribution(MooreTerrain::SmoothMare);
    const auto samples = Sample(smooth, 4u, 10.0, 1000.0);
    EXPECT_LT(
        KolmogorovSmirnov(samples, [&](double d) { return TruncatedCdf(smooth, 10.0, 1000.0, d); }),
        KolmogorovSmirnovCritical(kSamples));
}

TEST(LunarCrateringStatistics, ViperDiscontinuityBecomesAnAtomAtTheJoin) {
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
