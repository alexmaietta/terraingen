#include "support/statistics.h"
#include "terraingen/models/crater_ages.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using terraingen::CraterAgeModel;
using terraingen::LunarSurface;
using terraingen::NeukumLunarChronology;
using terraingen::RandomStream;
using terraingen::SurfaceModel;
using terraingen::UniformFreshness;
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
    const UniformFreshness uniform(1.5);
    const auto ages = SampleAges(uniform, 20.0, 1u);
    EXPECT_LT(KolmogorovSmirnov(ages, [](double age) { return age / 1.5; }),
              KolmogorovSmirnovCritical(kSamples));
}

TEST(CraterAgeStatistics, SteadyStateAgesFollowTheChronologyInBothRegimes) {
    const SurfaceModel surface = LunarSurface(3.5);
    for (const double d : {10.0, 1000.0}) {
        const double lifetime = surface.ages->LifetimeGyr(d);
        const auto ages = SampleAges(*surface.ages, d, d == 10.0 ? 2u : 3u);
        EXPECT_LT(KolmogorovSmirnov(ages,
                                    [lifetime](double age) {
                                        return NeukumLunarChronology(age) /
                                               NeukumLunarChronology(lifetime);
                                    }),
                  KolmogorovSmirnovCritical(kSamples))
            << d;
    }
}

TEST(CraterAgeStatistics, ShortLifetimeAgesAreNearlyUniform) {
    const SurfaceModel surface = LunarSurface(3.5);
    const double lifetime = surface.ages->LifetimeGyr(10.0);
    const auto ages = SampleAges(*surface.ages, 10.0, 4u);
    EXPECT_LT(KolmogorovSmirnov(ages, [lifetime](double age) { return age / lifetime; }),
              KolmogorovSmirnovCritical(kSamples));
}

TEST(CraterAgeStatistics, LunarSurfaceDiametersMatchTheirCdfAcrossTheCrossover) {
    const SurfaceModel surface = LunarSurface(3.5);
    RandomStream stream(9u, 9u);
    std::vector<double> diameters(kSamples);
    for (double& d : diameters) {
        d = surface.density->SampleDiameter(stream, 50.0, 2000.0);
    }
    const double top = surface.density->Cumulative(50.0);
    const double bottom = surface.density->Cumulative(2000.0);
    EXPECT_LT(KolmogorovSmirnov(diameters,
                                [&](double d) {
                                    return (top - surface.density->Cumulative(d)) / (top - bottom);
                                }),
              KolmogorovSmirnovCritical(kSamples));
}
