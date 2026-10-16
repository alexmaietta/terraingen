#include "support/lunar_craters.h"
#include "support/statistics.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using terraingen::CraterAgeModel;
using terraingen::NeukumLunarChronology;
using terraingen::RandomStream;
using terraingen::UniformAges;
using terraingen::testing::KolmogorovSmirnov;
using terraingen::testing::KolmogorovSmirnovCritical;

namespace {

constexpr std::size_t kSamples = 40000;

std::vector<double> SampleAges(const CraterAgeModel& model, double diameter, std::uint32_t seed) {
    RandomStream stream(seed, 31u);
    std::vector<double> ages(kSamples);
    for (double& age : ages) {
        age = model.SampleAgeGyr(stream, diameter);
    }
    return ages;
}

}  // namespace

TEST(CraterAgeStatistics, UniformFreshnessAgesAreUniform) {
    const UniformAges uniform(1.5);
    const auto ages = SampleAges(uniform, 20.0, 1u);
    EXPECT_LT(KolmogorovSmirnov(ages, [](double age) { return age / 1.5; }),
              KolmogorovSmirnovCritical(kSamples));
}

TEST(CraterAgeStatistics, SteadyStateAgesFollowTheChronologyInBothRegimes) {
    const auto model = terraingen::testing::LunarCraterAges(3.5);
    for (const double d : {10.0, 1000.0}) {
        const double lifetime = model->LifetimeGyr(d);
        const auto samples = SampleAges(*model, d, d == 10.0 ? 2u : 3u);
        EXPECT_LT(KolmogorovSmirnov(samples,
                                    [lifetime](double age) {
                                        return NeukumLunarChronology(age) /
                                               NeukumLunarChronology(lifetime);
                                    }),
                  KolmogorovSmirnovCritical(kSamples))
            << d;
    }
}

TEST(CraterAgeStatistics, ShortLifetimeAgesAreNearlyUniform) {
    const auto model = terraingen::testing::LunarCraterAges(3.5);
    const double lifetime = model->LifetimeGyr(10.0);
    const auto samples = SampleAges(*model, 10.0, 4u);
    EXPECT_LT(KolmogorovSmirnov(samples, [lifetime](double age) { return age / lifetime; }),
              KolmogorovSmirnovCritical(kSamples));
}

TEST(CraterAgeStatistics, LunarSurfaceDiametersMatchTheirCdfAcrossTheCrossover) {
    const auto density = terraingen::testing::LunarCraterDensity(3.5);
    RandomStream stream(9u, 9u);
    std::vector<double> diameters(kSamples);
    for (double& d : diameters) {
        d = density->SampleDiameter(stream, 50.0, 2000.0);
    }
    const double top = density->Cumulative(50.0);
    const double bottom = density->Cumulative(2000.0);
    EXPECT_LT(
        KolmogorovSmirnov(
            diameters, [&](double d) { return (top - density->Cumulative(d)) / (top - bottom); }),
        KolmogorovSmirnovCritical(kSamples));
}
