#include "support/lunar_craters.h"
#include "support/statistics.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using terraingen::Bounds2d;
using terraingen::CraterList;
using terraingen::FreshAges;
using terraingen::GenerateCraters;
using terraingen::PlanarCells;
using terraingen::PowerLawSfd;
using terraingen::testing::ChiSquareCritical;
using terraingen::testing::KolmogorovSmirnov;
using terraingen::testing::KolmogorovSmirnovCritical;

namespace {

/// The default generation options with diameters from `dMin` to `dMax` (m).
terraingen::CraterGenerationOptions Diameters(double dMin, double dMax) {
    terraingen::CraterGenerationOptions options;
    options.dMin = dMin;
    options.dMax = dMax;
    return options;
}

}  // namespace

TEST(CraterPopulationStatistics, CountMatchesTheExpectedCountOverTheRegion) {
    const auto density = terraingen::testing::LunarCraterDensity(3.5);
    const auto ages = terraingen::testing::LunarCraterAges(3.5);
    const Bounds2d region{-750.0, -500.0, 750.0, 1000.0};
    const double area = 1500.0 * 1500.0;
    const CraterList craters =
        GenerateCraters(PlanarCells(), region, *density, *ages, 21u, Diameters(2.5, 1000.0));
    const double mean = density->ExpectedCount(area, 2.5, 1000.0);
    EXPECT_NEAR(static_cast<double>(craters.Size()), mean, 5.0 * std::sqrt(mean));
}

TEST(CraterPopulationStatistics, DiametersMatchTheDensityAcrossBands) {
    const auto density = terraingen::testing::LunarCraterDensity(3.5);
    const auto ages = terraingen::testing::LunarCraterAges(3.5);
    const CraterList craters = GenerateCraters(PlanarCells(), {0.0, 0.0, 1200.0, 1200.0}, *density,
                                               *ages, 22u, Diameters(3.0, 300.0));
    const double top = density->Cumulative(3.0);
    const double bottom = density->Cumulative(300.0);
    ASSERT_GT(craters.Size(), 10000u);
    EXPECT_LT(KolmogorovSmirnov(
                  craters.Diameters(),
                  [&](double d) { return (top - density->Cumulative(d)) / (top - bottom); }),
              KolmogorovSmirnovCritical(craters.Size()));
}

TEST(CraterPopulationStatistics, PositionsAreUniformAcrossQuadratsNotAlignedWithCells) {
    const PowerLawSfd density(0.1, 2.0);
    const Bounds2d region{3.0, 7.0, 1003.0, 1007.0};
    const CraterList craters =
        GenerateCraters(PlanarCells(), region, density, FreshAges(), 23u, Diameters(1.0, 64.0));
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

namespace {

/// The area in m^2 of a longitude-latitude box on `surface`, by midpoint quadrature.
double BoxArea(const terraingen::Surface& surface, double lon1, double lon2, double lat1,
               double lat2) {
    constexpr int kSteps = 60;
    constexpr double kRadian = terraingen::kPi / 180.0;
    double area = 0.0;
    for (int j = 0; j < kSteps; ++j) {
        const double lat = lat1 + (lat2 - lat1) * (j + 0.5) / kSteps;
        for (int i = 0; i < kSteps; ++i) {
            const double lon = lon1 + (lon2 - lon1) * (i + 0.5) / kSteps;
            area += surface.AreaPerSteradian({lon, lat}) * std::cos(lat * kRadian);
        }
    }
    return area * (lon2 - lon1) * (lat2 - lat1) * kRadian * kRadian / (kSteps * kSteps);
}

}  // namespace

TEST(CraterPopulationStatistics, AnEllipsoidHoldsTheDensityTimesItsArea) {
    const terraingen::Surface body = terraingen::Surface::Ellipsoid(270.0, 147.0, 104.0);
    const PowerLawSfd density(0.1, 2.0);
    terraingen::CraterGenerationOptions options = Diameters(1.0, 20.0);
    options.surface = body;
    const CraterList craters =
        GenerateCraters(terraingen::CubeSphereCells(body), {-180.0, -90.0, 180.0, 90.0}, density,
                        FreshAges(), 24u, options);
    const double mean = density.ExpectedCount(BoxArea(body, -180.0, 180.0, -90.0, 90.0), 1.0, 20.0);
    EXPECT_NEAR(static_cast<double>(craters.Size()), mean, 5.0 * std::sqrt(mean));
}

TEST(CraterPopulationStatistics, EllipsoidPositionsAreUniformByArea) {
    const terraingen::Surface body = terraingen::Surface::Ellipsoid(270.0, 147.0, 104.0);
    const PowerLawSfd density(0.1, 2.0);
    terraingen::CraterGenerationOptions options = Diameters(1.0, 20.0);
    options.surface = body;
    const CraterList craters =
        GenerateCraters(terraingen::CubeSphereCells(body), {-180.0, -90.0, 180.0, 90.0}, density,
                        FreshAges(), 25u, options);
    constexpr int kLon = 8;
    constexpr int kLat = 6;
    std::vector<double> counts(kLon * kLat, 0.0);
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const int i = std::min(kLon - 1, static_cast<int>((craters.X()[n] + 180.0) / 360.0 * kLon));
        const int j = std::min(kLat - 1, static_cast<int>((craters.Y()[n] + 90.0) / 180.0 * kLat));
        counts[static_cast<std::size_t>(j * kLon + i)] += 1.0;
    }
    const double total = BoxArea(body, -180.0, 180.0, -90.0, 90.0);
    double chiSquare = 0.0;
    for (int j = 0; j < kLat; ++j) {
        for (int i = 0; i < kLon; ++i) {
            const double area =
                BoxArea(body, -180.0 + 360.0 * i / kLon, -180.0 + 360.0 * (i + 1) / kLon,
                        -90.0 + 180.0 * j / kLat, -90.0 + 180.0 * (j + 1) / kLat);
            const double expected = static_cast<double>(craters.Size()) * area / total;
            const double count = counts[static_cast<std::size_t>(j * kLon + i)];
            chiSquare += (count - expected) * (count - expected) / expected;
        }
    }
    EXPECT_LT(chiSquare, ChiSquareCritical(kLon * kLat - 1.0));
}
