#include "terraingen/distribution/power_law.h"
#include "terraingen/pipeline/report.h"
#include "terraingen/util/geometry.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

using terraingen::Boulder;
using terraingen::BoulderFeatures;
using terraingen::BoulderModel;
using terraingen::Crater;
using terraingen::CraterBin;
using terraingen::CraterFeatures;
using terraingen::DiameterRange;
using terraingen::GeoTransform;
using terraingen::GridReport;
using terraingen::kDegree;
using terraingen::kPi;
using terraingen::kRadian;
using terraingen::LayerTimer;
using terraingen::LayerTiming;
using terraingen::PowerLawSfd;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::ReportGrid;
using terraingen::ReportToJson;
using terraingen::SfdKind;
using terraingen::Surface;
using terraingen::TerrainReport;

namespace {

/// A north-up raster of `width` x `height` cells of `pixelSize` meters with its west edge at 0
/// and north edge at `height * pixelSize`, each cell `height(x, y)` at its center.
template <typename Height>
Raster<float> PlaneRaster(std::size_t width, std::size_t height, double pixelSize,
                          const Height& heightAt) {
    const RasterGrid grid{
        width, height,
        GeoTransform::NorthUp(0.0, static_cast<double>(height) * pixelSize, pixelSize)};
    Raster<float> raster = grid.MakeRaster();
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            const terraingen::Vec2d center = raster.CellCenter(col, row);
            raster(col, row) = static_cast<float>(heightAt(center.x, center.y));
        }
    }
    return raster;
}

GridReport Report(const Raster<float>& heights, const Surface& surface = Surface{}) {
    return ReportGrid(heights, surface, nullptr, nullptr, nullptr, 2);
}

/// The interpolated `q` quantile of `values`.
double Quantile(std::vector<double> values, double q) {
    std::sort(values.begin(), values.end());
    const double position = q * static_cast<double>(values.size() - 1);
    const auto rank = static_cast<std::size_t>(position);
    const double fraction = position - static_cast<double>(rank);
    return rank + 1 < values.size() ? values[rank] + fraction * (values[rank + 1] - values[rank])
                                    : values[rank];
}

CraterFeatures CratersWithDiameters(const std::vector<double>& diameters) {
    CraterFeatures features;
    for (const double diameter : diameters) {
        Crater crater;
        crater.diameter = diameter;
        features.craters.Append(crater);
    }
    return features;
}

}  // namespace

TEST(ReportGrid, AFlatGridHasOneHeightAndNoSlope) {
    const Raster<float> flat = PlaneRaster(8, 6, 2.0, [](double, double) { return 5.0; });
    const GridReport report = Report(flat);
    EXPECT_EQ(report.grid, RasterGrid::FromRaster(flat));
    EXPECT_EQ(report.heightMin, 5.0);
    EXPECT_EQ(report.heightMax, 5.0);
    EXPECT_EQ(report.heightMean, 5.0);
    EXPECT_EQ(report.heightStd, 0.0);
    EXPECT_EQ(report.slopeMeanDeg, 0.0);
    EXPECT_EQ(report.slopeMedianDeg, 0.0);
    EXPECT_EQ(report.slopeP95Deg, 0.0);
    EXPECT_TRUE(report.craterBins.empty());
    EXPECT_EQ(report.boulderCount, 0u);
    EXPECT_EQ(report.boulderModelCount, 0u);
    EXPECT_EQ(report.boulderCover, 0.0);
}

TEST(ReportGrid, APlaneHasItsSlopeEverywhere) {
    const Raster<float> plane =
        PlaneRaster(9, 7, 2.0, [](double x, double y) { return 0.3 * x + 0.4 * y; });
    const GridReport report = Report(plane);
    const double expected = std::atan(0.5) * kRadian;
    EXPECT_NEAR(report.slopeMeanDeg, expected, 1e-5);
    EXPECT_NEAR(report.slopeMedianDeg, expected, 1e-5);
    EXPECT_NEAR(report.slopeP95Deg, expected, 1e-5);
}

TEST(ReportGrid, ARampHasExactHeightStatistics) {
    const Raster<float> ramp = PlaneRaster(5, 3, 1.0, [](double x, double) { return x - 0.5; });
    const GridReport report = Report(ramp);
    EXPECT_EQ(report.heightMin, 0.0);
    EXPECT_EQ(report.heightMax, 4.0);
    EXPECT_EQ(report.heightMean, 2.0);
    EXPECT_DOUBLE_EQ(report.heightStd, std::sqrt(2.0));
    EXPECT_NEAR(report.slopeMeanDeg, 45.0, 1e-12);
}

TEST(ReportGrid, SlopeQuantilesFollowTheCellGradients) {
    const Raster<float> bowl =
        PlaneRaster(21, 2, 1.0, [](double x, double) { return 0.5 * x * x; });
    std::vector<double> gradients{1.0};
    for (int col = 1; col < 20; ++col) {
        gradients.push_back(col + 0.5);
    }
    gradients.push_back(20.0);
    std::vector<double> slopes;
    for (int row = 0; row < 2; ++row) {
        for (const double gradient : gradients) {
            slopes.push_back(std::atan(gradient) * kRadian);
        }
    }
    const GridReport report = Report(bowl);
    EXPECT_NEAR(report.slopeMeanDeg,
                std::accumulate(slopes.begin(), slopes.end(), 0.0) /
                    static_cast<double>(slopes.size()),
                1e-12);
    EXPECT_NEAR(report.slopeMedianDeg, Quantile(slopes, 0.5), 1e-12);
    EXPECT_NEAR(report.slopeP95Deg, Quantile(slopes, 0.95), 1e-12);
    EXPECT_NEAR(report.slopeMedianDeg, std::atan(10.5) * kRadian, 1e-12);
    EXPECT_NEAR(report.slopeP95Deg, std::atan(19.5) * kRadian, 1e-12);
}

TEST(ReportGrid, SkipsCellsWithoutData) {
    Raster<float> plane = PlaneRaster(6, 5, 1.0, [](double x, double) { return 2.0 * x; });
    plane.SetNoData(-9999.0f);
    plane(0, 0) = -9999.0f;
    plane(3, 2) = std::numeric_limits<float>::quiet_NaN();
    plane(5, 4) = -9999.0f;
    const GridReport report = Report(plane);
    EXPECT_EQ(report.heightMin, 1.0);
    EXPECT_EQ(report.heightMax, 11.0);
    EXPECT_TRUE(std::isfinite(report.heightMean));
    const double expected = std::atan(2.0) * kRadian;
    EXPECT_NEAR(report.slopeMeanDeg, expected, 1e-12);
    EXPECT_NEAR(report.slopeMedianDeg, expected, 1e-12);
    EXPECT_NEAR(report.slopeP95Deg, expected, 1e-12);
}

TEST(ReportGrid, AGridWithoutDataHasNoStatistics) {
    Raster<float> empty = PlaneRaster(
        3, 3, 1.0, [](double, double) { return std::numeric_limits<double>::quiet_NaN(); });
    const GridReport report = Report(empty);
    EXPECT_TRUE(std::isnan(report.heightMin));
    EXPECT_TRUE(std::isnan(report.heightStd));
    EXPECT_TRUE(std::isnan(report.slopeMeanDeg));
    EXPECT_TRUE(std::isnan(report.slopeP95Deg));
}

TEST(ReportGrid, ACellWithoutNeighborsHasNoSlope) {
    Raster<float> lone = PlaneRaster(1, 1, 1.0, [](double, double) { return 3.0; });
    const GridReport report = Report(lone);
    EXPECT_EQ(report.heightMean, 3.0);
    EXPECT_TRUE(std::isnan(report.slopeMedianDeg));
}

TEST(ReportGrid, SlopesOnABodyUseMetersOnTheSurface) {
    const double radius = 1737400.0;
    const Surface moon = Surface::Sphere(radius);
    const RasterGrid grid{16, 12, GeoTransform::NorthUp(10.0, 5.0, 0.01)};
    Raster<float> heights = grid.MakeRaster();
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const double latitude = heights.CellCenter(col, row).y;
            heights(col, row) = static_cast<float>(0.1 * radius * (latitude - 4.9) * kDegree);
        }
    }
    const GridReport report = Report(heights, moon);
    const double expected = std::atan(0.1) * kRadian;
    EXPECT_NEAR(report.slopeMeanDeg, expected, 1e-3);
    EXPECT_NEAR(report.slopeMedianDeg, expected, 1e-3);
    EXPECT_NEAR(report.slopeP95Deg, expected, 1e-3);
}

TEST(ReportGrid, BinsCratersByPowersOfTwo) {
    const Raster<float> flat = PlaneRaster(10, 10, 10.0, [](double, double) { return 0.0; });
    const CraterFeatures craters = CratersWithDiameters({1.5, 3.0, 3.9, 12.0, 2.0});
    const GridReport report = ReportGrid(flat, Surface{}, &craters, nullptr, nullptr);
    ASSERT_EQ(report.craterBins.size(), 4u);
    const std::vector<std::size_t> counts{1, 3, 0, 1};
    for (std::size_t n = 0; n < counts.size(); ++n) {
        const CraterBin& bin = report.craterBins[n];
        EXPECT_EQ(bin.minDiameter, std::ldexp(1.0, static_cast<int>(n)));
        EXPECT_EQ(bin.maxDiameter, 2.0 * bin.minDiameter);
        EXPECT_EQ(bin.count, counts[n]);
        EXPECT_TRUE(std::isnan(bin.expected));
    }
}

TEST(ReportGrid, ExpectedCraterCountsComeFromTheTarget) {
    const Raster<float> flat = PlaneRaster(10, 10, 10.0, [](double, double) { return 0.0; });
    const CraterFeatures craters = CratersWithDiameters({1.5, 3.0, 12.0});
    const PowerLawSfd target{0.01, 2.0};
    const GridReport report = ReportGrid(flat, Surface{}, &craters, nullptr, &target);
    ASSERT_EQ(report.craterBins.size(), 4u);
    const double area = 100.0 * 100.0;
    for (const CraterBin& bin : report.craterBins) {
        const double expected =
            area * 0.01 * (std::pow(bin.minDiameter, -2.0) - std::pow(bin.maxDiameter, -2.0));
        EXPECT_NEAR(bin.expected, expected, 1e-12 * expected);
    }
    EXPECT_NEAR(report.craterBins[0].expected, 75.0, 1e-9);
}

TEST(ReportGrid, ExpectedCountsAreNanOutsideTheTarget) {
    const Raster<float> flat = PlaneRaster(10, 10, 10.0, [](double, double) { return 0.0; });
    const CraterFeatures craters = CratersWithDiameters({1.5, 3.0});
    const PowerLawSfd bounded{0.01, 2.0, SfdKind::Density, DiameterRange{2.0, 100.0}};
    const GridReport inRange = ReportGrid(flat, Surface{}, &craters, nullptr, &bounded);
    ASSERT_EQ(inRange.craterBins.size(), 2u);
    EXPECT_TRUE(std::isnan(inRange.craterBins[0].expected));
    EXPECT_GT(inRange.craterBins[1].expected, 0.0);
    const PowerLawSfd production{0.01, 2.0, SfdKind::ProductionRate};
    const GridReport rate = ReportGrid(flat, Surface{}, &craters, nullptr, &production);
    EXPECT_TRUE(std::isnan(rate.craterBins[1].expected));
}

TEST(ReportGrid, CountsBouldersModelsAndCover) {
    const Raster<float> flat = PlaneRaster(10, 5, 2.0, [](double, double) { return 0.0; });
    BoulderFeatures boulders;
    for (const double diameter : {1.0, 2.0, 2.0}) {
        Boulder boulder;
        boulder.diameter = diameter;
        boulders.boulders.push_back(boulder);
    }
    boulders.boulders.back().model = true;
    boulders.models.push_back(BoulderModel{});
    const GridReport report = ReportGrid(flat, Surface{}, nullptr, &boulders, nullptr);
    EXPECT_EQ(report.boulderCount, 3u);
    EXPECT_EQ(report.boulderModelCount, 1u);
    EXPECT_DOUBLE_EQ(report.boulderCover, 0.25 * kPi * 9.0 / 200.0);
}

TEST(LayerTimer, SumsEachLayersTimeInSeconds) {
    LayerTimer timer{{"noise", "craters"}};
    timer.Add(1, 1500.0);
    timer.Add(1, 500.0);
    timer.Add(0, 250.0);
    const std::vector<LayerTiming>& timings = timer.Timings();
    ASSERT_EQ(timings.size(), 2u);
    EXPECT_EQ(timings[0].type, "noise");
    EXPECT_DOUBLE_EQ(timings[0].seconds, 0.25);
    EXPECT_EQ(timings[1].type, "craters");
    EXPECT_DOUBLE_EQ(timings[1].seconds, 2.0);
    EXPECT_THROW(timer.Add(2, 1.0), std::out_of_range);
}

TEST(ReportToJson, RoundTripsEveryField) {
    const Raster<float> ramp = PlaneRaster(5, 3, 1.0, [](double x, double) { return x - 0.5; });
    const CraterFeatures craters = CratersWithDiameters({1.5, 3.0});
    BoulderFeatures boulders;
    boulders.boulders.push_back(Boulder{});
    TerrainReport report;
    report.grids.push_back(ReportGrid(ramp, Surface{}, &craters, &boulders, nullptr));
    report.layers = {{"noise", 0.125}, {"craters", 1.0 / 3.0}};
    report.backend = "cpu";
    report.threads = 4;
    report.seeds = {7u, 4000000000u};
    report.seconds = 2.5;
    const nlohmann::json json = nlohmann::json::parse(ReportToJson(report));
    EXPECT_EQ(json["version"], report.version);
    EXPECT_FALSE(report.version.empty());
    EXPECT_EQ(json["backend"], "cpu");
    EXPECT_EQ(json["threads"], 4);
    EXPECT_EQ(json["seeds"], nlohmann::json::array({7u, 4000000000u}));
    EXPECT_EQ(json["seconds"], 2.5);
    ASSERT_EQ(json["layers"].size(), 2u);
    EXPECT_EQ(json["layers"][1]["type"], "craters");
    EXPECT_EQ(json["layers"][1]["seconds"].get<double>(), 1.0 / 3.0);
    ASSERT_EQ(json["grids"].size(), 1u);
    const nlohmann::json& grid = json["grids"][0];
    EXPECT_EQ(grid["grid"]["width"], 5);
    EXPECT_EQ(grid["grid"]["height"], 3);
    EXPECT_EQ(grid["grid"]["pixel_size"], 1.0);
    EXPECT_EQ(grid["grid"]["extent"], nlohmann::json::array({0.0, 0.0, 5.0, 3.0}));
    EXPECT_EQ(grid["grid"]["transform"], nlohmann::json::array({0.0, 1.0, 0.0, 3.0, 0.0, -1.0}));
    EXPECT_EQ(grid["height"]["mean"], 2.0);
    EXPECT_EQ(grid["height"]["std"].get<double>(), report.grids[0].heightStd);
    EXPECT_EQ(grid["slope_deg"]["median"].get<double>(), report.grids[0].slopeMedianDeg);
    ASSERT_EQ(grid["crater_bins"].size(), 2u);
    EXPECT_EQ(grid["crater_bins"][1]["min_diameter"], 2.0);
    EXPECT_EQ(grid["crater_bins"][1]["count"], 1);
    EXPECT_TRUE(grid["crater_bins"][1]["expected"].is_null());
    EXPECT_EQ(grid["boulders"]["count"], 1);
    EXPECT_EQ(grid["boulders"]["models"], 0);
    EXPECT_EQ(grid["boulders"]["cover"], 0.0);
}

TEST(ReportToJson, WritesOneLineForANegativeIndent) {
    const std::string text = ReportToJson(TerrainReport{}, -1);
    EXPECT_EQ(text.find('\n'), std::string::npos);
    EXPECT_NE(ReportToJson(TerrainReport{}).find("\n  \"backend\""), std::string::npos);
}
