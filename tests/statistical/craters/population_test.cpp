#include "support/statistics.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using terraingen::Bounds2d;
using terraingen::CraterList;
using terraingen::FreshAges;
using terraingen::GenerateCraters;
using terraingen::LunarSurface;
using terraingen::PlanarCells;
using terraingen::PowerLawSfd;
using terraingen::SurfaceModel;
using terraingen::testing::ChiSquareCritical;
using terraingen::testing::KolmogorovSmirnov;
using terraingen::testing::KolmogorovSmirnovCritical;

TEST(CraterPopulationStatistics, CountMatchesTheExpectedCountOverTheRegion) {
    const SurfaceModel surface = LunarSurface(3.5);
    const Bounds2d region{-750.0, -500.0, 750.0, 1000.0};
    const double area = 1500.0 * 1500.0;
    const CraterList craters = GenerateCraters(PlanarCells(), region, *surface.density,
                                               *surface.ages, 21u, {.dMin = 2.5, .dMax = 1000.0});
    const double mean = surface.density->ExpectedCount(area, 2.5, 1000.0);
    EXPECT_NEAR(static_cast<double>(craters.Size()), mean, 5.0 * std::sqrt(mean));
}

TEST(CraterPopulationStatistics, DiametersMatchTheDensityAcrossBands) {
    const SurfaceModel surface = LunarSurface(3.5);
    const CraterList craters =
        GenerateCraters(PlanarCells(), {0.0, 0.0, 1200.0, 1200.0}, *surface.density, *surface.ages,
                        22u, {.dMin = 3.0, .dMax = 300.0});
    const double top = surface.density->Cumulative(3.0);
    const double bottom = surface.density->Cumulative(300.0);
    ASSERT_GT(craters.Size(), 10000u);
    EXPECT_LT(KolmogorovSmirnov(craters.Diameters(),
                                [&](double d) {
                                    return (top - surface.density->Cumulative(d)) / (top - bottom);
                                }),
              KolmogorovSmirnovCritical(craters.Size()));
}

TEST(CraterPopulationStatistics, PositionsAreUniformAcrossQuadratsNotAlignedWithCells) {
    const PowerLawSfd density(0.1, 2.0);
    const Bounds2d region{3.0, 7.0, 1003.0, 1007.0};
    const CraterList craters = GenerateCraters(PlanarCells(), region, density, FreshAges(), 23u,
                                               {.dMin = 1.0, .dMax = 64.0});
    constexpr std::size_t kSide = 7;
    std::vector<double> counts(kSide * kSide, 0.0);
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const auto qx = static_cast<std::size_t>((craters.X()[n] - region.minX) / 1000.0 *
                                                 static_cast<double>(kSide));
        const auto qy = static_cast<std::size_t>((craters.Y()[n] - region.minY) / 1000.0 *
                                                 static_cast<double>(kSide));
        counts[qy * kSide + qx] += 1.0;
    }
    const auto quadrats = static_cast<double>(counts.size());
    const double expected = static_cast<double>(craters.Size()) / quadrats;
    double chiSquare = 0.0;
    for (const double count : counts) {
        chiSquare += (count - expected) * (count - expected) / expected;
    }
    EXPECT_LT(chiSquare, ChiSquareCritical(quadrats - 1.0));
    EXPECT_LT(KolmogorovSmirnov(craters.X(), [&](double x) { return (x - region.minX) / 1000.0; }),
              KolmogorovSmirnovCritical(craters.Size()));
}
