#include "terraingen/albedo/brightness_statistics.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using terraingen::Albedo;
using terraingen::AlbedoContext;
using terraingen::AlbedoSample;
using terraingen::EvaluateAlbedo;
using terraingen::EvaluateAlbedoAt;
using terraingen::GeoTransform;
using terraingen::HsvToRgb;
using terraingen::NoiseLayer;
using terraingen::OpticalMaturity;
using terraingen::Raster;
using terraingen::RasterAlbedo;
using terraingen::RasterAlbedoOptions;
using terraingen::RasterGrid;
using terraingen::ReflectanceAtMaturity;
using terraingen::SyntheticAlbedo;
using terraingen::SyntheticAlbedoOptions;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

RasterGrid Grid(std::size_t side, double cell, double originX = -50.0, double originY = 50.0) {
    return {side, side, GeoTransform::NorthUp(originX, originY, cell)};
}

NoiseLayer Noise(float amplitude, float wavelength, std::uint32_t seed) {
    NoiseLayer layer;
    layer.fractal.amplitude = amplitude;
    layer.fractal.wavelength = wavelength;
    layer.fractal.octaves = 4;
    layer.fractal.seed = seed;
    return layer;
}

class TwoBands final : public terraingen::AlbedoSource {
  public:
    std::size_t Bands() const override {
        return 2;
    }
    AlbedoSample ValueAt(double, double, const AlbedoContext&) const override {
        return {};
    }
};

class Doubling final : public terraingen::AlbedoLayer {
  public:
    void ApplyAt(AlbedoSample& value, std::size_t bands, double, double,
                 const AlbedoContext&) const override {
        for (std::size_t band = 0; band < bands; ++band) {
            value[band] *= 2.0f;
        }
    }
};

/// A map whose log brightness rises by `gradient` per cell along rows and columns.
Raster<float> Ramp(std::size_t side, double gradient) {
    Raster<float> map(side, side);
    map.SetTransform(GeoTransform::NorthUp(0.0, static_cast<double>(side) * 10.0, 10.0));
    for (std::size_t row = 0; row < side; ++row) {
        for (std::size_t col = 0; col < side; ++col) {
            map(col, row) =
                static_cast<float>(0.1 * std::exp(gradient * static_cast<double>(col + row)));
        }
    }
    return map;
}

}  // namespace

TEST(OpticalMaturity, MatchesLuceyTable1WithTheLaboratoryOrigin) {
    EXPECT_NEAR(OpticalMaturity(0.072, 0.080, terraingen::kLaboratoryMaturityOrigin), 0.162, 1e-3);
    EXPECT_NEAR(OpticalMaturity(0.354, 0.370, terraingen::kLaboratoryMaturityOrigin), 0.405, 1e-3);
    EXPECT_NEAR(OpticalMaturity(0.08, 0.08 * 1.19), 0.0, 1e-12);
    EXPECT_THROW(OpticalMaturity(0.0, 0.1), std::invalid_argument);
    EXPECT_THROW(OpticalMaturity(0.1, kNaN), std::invalid_argument);
}

TEST(ReflectanceAtMaturity, MovesFromTheOriginInProportionToMaturity) {
    EXPECT_NEAR(ReflectanceAtMaturity(0.2, 0.2, 0.2), 0.2, 1e-15);
    EXPECT_NEAR(ReflectanceAtMaturity(0.2, 0.1, 0.2), 0.32, 1e-15);
    EXPECT_NEAR(ReflectanceAtMaturity(0.2, 0.2, 0.1), 0.14, 1e-15);
    EXPECT_DOUBLE_EQ(ReflectanceAtMaturity(0.05, 0.2, 0.4), 0.05);
    EXPECT_THROW(ReflectanceAtMaturity(0.2, 0.0, 0.1), std::invalid_argument);
    EXPECT_THROW(ReflectanceAtMaturity(0.2, 0.2, -0.1), std::invalid_argument);
    EXPECT_THROW(ReflectanceAtMaturity(0.2, 0.2, 0.1, kNaN), std::invalid_argument);
}

TEST(HsvToRgb, FollowsTheHexcone) {
    const auto expect = [](const AlbedoSample& got, float r, float g, float b) {
        EXPECT_NEAR(got[0], r, 1e-6f);
        EXPECT_NEAR(got[1], g, 1e-6f);
        EXPECT_NEAR(got[2], b, 1e-6f);
    };
    expect(HsvToRgb(0.0, 1.0, 1.0), 1.0f, 0.0f, 0.0f);
    expect(HsvToRgb(60.0, 1.0, 1.0), 1.0f, 1.0f, 0.0f);
    expect(HsvToRgb(120.0, 1.0, 1.0), 0.0f, 1.0f, 0.0f);
    expect(HsvToRgb(180.0, 1.0, 1.0), 0.0f, 1.0f, 1.0f);
    expect(HsvToRgb(240.0, 1.0, 0.5), 0.0f, 0.0f, 0.5f);
    expect(HsvToRgb(-60.0, 1.0, 1.0), 1.0f, 0.0f, 1.0f);
    expect(HsvToRgb(390.0, 0.5, 0.8), 0.8f, 0.6f, 0.4f);
    expect(HsvToRgb(77.0, 0.0, 0.3), 0.3f, 0.3f, 0.3f);
}

TEST(SyntheticAlbedo, DefaultsToAUniformMareReflectance) {
    const Albedo albedo{std::make_shared<SyntheticAlbedo>(SyntheticAlbedoOptions{}), {}};
    const auto bands = EvaluateAlbedo(albedo, Grid(8, 1.0), {});
    ASSERT_EQ(bands.size(), 1u);
    for (std::size_t i = 0; i < bands[0].Size(); ++i) {
        EXPECT_FLOAT_EQ(bands[0].Values()[i], 0.110f);
    }
}

TEST(SyntheticAlbedo, ColorAndNoiseVaryEachChannel) {
    SyntheticAlbedoOptions params;
    params.rgb = true;
    params.hue = 30.0;
    params.saturation = 0.2;
    params.value = 0.3;
    params.hueNoise = {Noise(10.0f, 40.0f, 1)};
    params.saturationNoise = {Noise(0.1f, 40.0f, 2)};
    params.valueNoise = {Noise(0.5f, 20.0f, 3)};
    const Albedo albedo{std::make_shared<SyntheticAlbedo>(params, terraingen::Vec2d{5.0, 5.0}), {}};
    const auto bands = EvaluateAlbedo(albedo, Grid(64, 1.0), {1.0, 4});
    ASSERT_EQ(bands.size(), 3u);
    double sum = 0.0;
    float lowest = 1.0f;
    float highest = 0.0f;
    for (std::size_t i = 0; i < bands[0].Size(); ++i) {
        const float r = bands[0].Values()[i];
        const float b = bands[2].Values()[i];
        EXPECT_GE(r, b);
        EXPECT_LE(r, 1.0f);
        sum += static_cast<double>(r);
        lowest = std::min(lowest, r);
        highest = std::max(highest, r);
    }
    EXPECT_NEAR(sum / static_cast<double>(bands[0].Size()), 0.3, 0.06);
    EXPECT_LT(lowest, 0.25f);
    EXPECT_GT(highest, 0.35f);
}

TEST(SyntheticAlbedo, ClampsTheValue) {
    SyntheticAlbedoOptions params;
    params.value = 0.9;
    params.valueNoise = {Noise(3.0f, 10.0f, 4)};
    const Albedo albedo{std::make_shared<SyntheticAlbedo>(params), {}};
    const auto bands = EvaluateAlbedo(albedo, Grid(32, 1.0), {});
    for (const float value : bands[0].Values()) {
        EXPECT_GE(value, 0.0f);
        EXPECT_LE(value, 1.0f);
    }
}

TEST(SyntheticAlbedo, RejectsInvalidParameters) {
    SyntheticAlbedoOptions params;
    params.hue = kInf;
    EXPECT_THROW(SyntheticAlbedo{params}, std::invalid_argument);
    params = {};
    params.saturation = 1.5;
    EXPECT_THROW(SyntheticAlbedo{params}, std::invalid_argument);
    params = {};
    params.value = -0.1;
    EXPECT_THROW(SyntheticAlbedo{params}, std::invalid_argument);
    params = {};
    params.valueNoise.assign(terraingen::kMaxNoiseLayers + 1, Noise(0.1f, 10.0f, 0));
    EXPECT_THROW(SyntheticAlbedo{params}, std::invalid_argument);
    params = {};
    params.hueNoise = {Noise(0.1f, -10.0f, 0)};
    EXPECT_THROW(SyntheticAlbedo{params}, std::invalid_argument);
}

TEST(EvaluateAlbedo, PointsEqualGridCellsAndThreadsDoNotMatter) {
    SyntheticAlbedoOptions params;
    params.rgb = true;
    params.saturation = 0.1;
    params.valueNoise = {Noise(0.3f, 7.0f, 9)};
    const Albedo albedo{std::make_shared<SyntheticAlbedo>(params), {std::make_shared<Doubling>()}};
    const RasterGrid grid = Grid(24, 0.5);
    const auto one = EvaluateAlbedo(albedo, grid, {0.5, 1});
    const auto many = EvaluateAlbedo(albedo, grid, {0.5, 0});
    for (std::size_t band = 0; band < 3; ++band) {
        const auto oneValues = one[band].Values();
        const auto manyValues = many[band].Values();
        EXPECT_TRUE(
            std::equal(oneValues.begin(), oneValues.end(), manyValues.begin(), manyValues.end()));
    }
    for (const auto& [col, row] :
         {std::pair<std::size_t, std::size_t>{0, 0}, std::pair<std::size_t, std::size_t>{5, 17},
          std::pair<std::size_t, std::size_t>{23, 23}}) {
        const auto center = one[0].CellCenter(col, row);
        const AlbedoSample point = EvaluateAlbedoAt(albedo, center.x, center.y, {0.5});
        for (std::size_t band = 0; band < 3; ++band) {
            EXPECT_EQ(point[band], one[band](col, row));
        }
    }
    const auto single = EvaluateAlbedoAt(
        Albedo{std::make_shared<SyntheticAlbedo>(SyntheticAlbedoOptions{}), {}}, 0.0, 0.0, {});
    EXPECT_FLOAT_EQ(single[0], 0.110f);
    EXPECT_EQ(single[1], 0.0f);
}

TEST(EvaluateAlbedo, RejectsInvalidInputs) {
    const auto source = std::make_shared<SyntheticAlbedo>(SyntheticAlbedoOptions{});
    EXPECT_THROW(EvaluateAlbedo(Albedo{}, Grid(4, 1.0), {}), std::invalid_argument);
    EXPECT_THROW(EvaluateAlbedo(Albedo{source, {nullptr}}, Grid(4, 1.0), {}),
                 std::invalid_argument);
    EXPECT_THROW(EvaluateAlbedo(Albedo{std::make_shared<TwoBands>(), {}}, Grid(4, 1.0), {}),
                 std::invalid_argument);
    EXPECT_THROW(EvaluateAlbedo(Albedo{source, {}}, RasterGrid{}, {}), std::invalid_argument);
    EXPECT_THROW(EvaluateAlbedo(Albedo{source, {}}, Grid(4, 1.0), {-1.0}), std::invalid_argument);
    EXPECT_NO_THROW(EvaluateAlbedo(Albedo{source, {}}, Grid(4, 1.0), {0.0}));
    EXPECT_THROW(EvaluateAlbedoAt(Albedo{source, {}}, 0.0, 0.0, {kNaN}), std::invalid_argument);
}

TEST(RasterAlbedo, MeasuresTheHurstExponentAndRoughness) {
    const RasterAlbedo ramp({Ramp(32, 0.01)});
    EXPECT_NEAR(ramp.Hurst(), 1.0, 1e-3);
    EXPECT_NEAR(ramp.CellRoughness(), 0.01 / std::sqrt(2.0), 1e-5);

    Raster<float> white(64, 64);
    white.SetTransform(GeoTransform::NorthUp(0.0, 640.0, 10.0));
    terraingen::RandomStream random(1, 2);
    for (float& value : white.Values()) {
        value = 0.1f + 0.05f * terraingen::HashToUnitFloat(random.NextU32());
    }
    const RasterAlbedo noisy({white});
    EXPECT_LT(noisy.Hurst(), 0.1);
    EXPECT_GT(noisy.CellRoughness(), 0.1);

    const RasterAlbedo flat({Raster<float>(8, 8, 0.2f)});
    EXPECT_EQ(flat.CellRoughness(), 0.0);
    EXPECT_EQ(flat.Hurst(), 0.5);

    RasterAlbedoOptions options;
    options.detail.hurst = 0.3;
    EXPECT_EQ(RasterAlbedo({Ramp(4, 0.01)}, options).Hurst(), 0.3);
    EXPECT_EQ(RasterAlbedo({Ramp(2, 0.01)}).Hurst(), 0.5);
}

TEST(RasterAlbedo, InterpolatesAndAddsDetailOnlyBelowItsResolution) {
    Raster<float> map = Ramp(32, 0.02);
    RasterAlbedoOptions plain;
    plain.detail.enabled = false;
    const RasterAlbedo smooth({map}, plain);
    const RasterAlbedo detailed({map});
    const AlbedoContext coarse{10.0};
    EXPECT_EQ(detailed.ValueAt(105.0, 205.0, coarse)[0], smooth.ValueAt(105.0, 205.0, coarse)[0]);
    EXPECT_FLOAT_EQ(smooth.ValueAt(105.0, 205.0, coarse)[0], map(10, 11));

    const AlbedoContext fine{0.25};
    double sum = 0.0;
    int count = 0;
    for (double y = 60.0; y < 260.0; y += 0.37) {
        const double x = 40.0 + 0.5 * y;
        const double ratio = static_cast<double>(
            std::log(detailed.ValueAt(x, y, fine)[0] / smooth.ValueAt(x, y, fine)[0]));
        sum += ratio * ratio;
        ++count;
    }
    const double rms = std::sqrt(sum / count);
    double expected = 0.0;
    for (int octave = 1; octave <= 4; ++octave) {
        expected += std::exp2(-2.0 * octave * detailed.Hurst());
    }
    expected = detailed.CellRoughness() * std::sqrt(expected);
    EXPECT_GT(rms, 0.5 * expected);
    EXPECT_LT(rms, 2.0 * expected);

    RasterAlbedoOptions none;
    none.detail.scale = 0.0;
    EXPECT_EQ(RasterAlbedo({map}, none).ValueAt(100.0, 200.0, fine)[0],
              smooth.ValueAt(100.0, 200.0, fine)[0]);
}

TEST(RasterAlbedo, PointsEqualGridCellsAndOutsideIsNoData) {
    std::vector<Raster<float>> bands{Ramp(16, 0.05), Ramp(16, 0.03), Ramp(16, 0.01)};
    bands[1].SetNoData(-1.0f);
    bands[1](3, 3) = -1.0f;
    const Albedo albedo{std::make_shared<RasterAlbedo>(bands), {}};
    const RasterGrid grid{40, 40, GeoTransform::NorthUp(-20.0, 180.0, 2.5)};
    const auto evaluated = EvaluateAlbedo(albedo, grid, {2.5, 3});
    ASSERT_EQ(evaluated.size(), 3u);
    EXPECT_TRUE(std::isnan(evaluated[0](0, 0)));
    EXPECT_TRUE(std::isnan(evaluated[2](0, 0)));
    for (const auto& [col, row] :
         {std::pair<std::size_t, std::size_t>{30, 30}, std::pair<std::size_t, std::size_t>{36, 25},
          std::pair<std::size_t, std::size_t>{39, 39}}) {
        const auto center = evaluated[0].CellCenter(col, row);
        const AlbedoSample point = EvaluateAlbedoAt(albedo, center.x, center.y, {2.5});
        for (std::size_t band = 0; band < 3; ++band) {
            EXPECT_FALSE(std::isnan(point[band]));
            EXPECT_EQ(point[band], evaluated[band](col, row));
        }
    }
    EXPECT_TRUE(std::isnan(EvaluateAlbedoAt(albedo, 35.0, 125.0, {2.5})[0]));
}

TEST(RasterAlbedo, RejectsInvalidMaps) {
    EXPECT_THROW(RasterAlbedo({Ramp(4, 0.1), Ramp(4, 0.1)}), std::invalid_argument);
    EXPECT_THROW(RasterAlbedo({Raster<float>()}), std::invalid_argument);
    Raster<float> rotated = Ramp(4, 0.1);
    rotated.SetTransform(GeoTransform{0.0, 1.0, 0.5, 0.0, 0.0, -1.0});
    EXPECT_THROW(RasterAlbedo({rotated}), std::invalid_argument);
    EXPECT_THROW(RasterAlbedo({Ramp(4, 0.1), Ramp(5, 0.1), Ramp(4, 0.1)}), std::invalid_argument);
    RasterAlbedoOptions options;
    options.detail.octaves = 0;
    EXPECT_THROW(RasterAlbedo({Ramp(4, 0.1)}, options), std::invalid_argument);
    options = {};
    options.detail.scale = -1.0;
    EXPECT_THROW(RasterAlbedo({Ramp(4, 0.1)}, options), std::invalid_argument);
    options = {};
    options.detail.hurst = 1.5;
    EXPECT_THROW(RasterAlbedo({Ramp(4, 0.1)}, options), std::invalid_argument);
    options = {};
    options.detail.fade.endSamples = 0.0;
    EXPECT_THROW(RasterAlbedo({Ramp(4, 0.1)}, options), std::invalid_argument);
}

TEST(SyntheticAlbedo, UsesSeamlessNoiseOnASphere) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    SyntheticAlbedoOptions params;
    params.rgb = true;
    params.saturation = 0.1;
    params.valueNoise = {Noise(0.3f, 2000.0f, 3)};
    params.hueNoise = {Noise(20.0f, 5000.0f, 4)};
    const SyntheticAlbedo albedo(params, {}, moon);
    const AlbedoContext context{10.0};
    const AlbedoSample east = albedo.ValueAt(180.0, 3.0, context);
    const AlbedoSample west = albedo.ValueAt(-180.0, 3.0, context);
    for (std::size_t band = 0; band < 3; ++band) {
        EXPECT_FLOAT_EQ(east[band], west[band]);
    }
    EXPECT_NE(albedo.ValueAt(10.0, 3.0, context)[0], east[0]);
    params.valueNoise[0].fractal.basis = terraingen::NoiseBasis::Simplex;
    const SyntheticAlbedo simplex(params, {}, moon);
    EXPECT_FLOAT_EQ(simplex.ValueAt(180.0, 3.0, context)[0],
                    simplex.ValueAt(-180.0, 3.0, context)[0]);
}

TEST(RasterAlbedo, AddsDetailInMetersOnASphere) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    Raster<float> map = Ramp(32, 0.02);
    const double step = 100.0 / moon.UnitLength();
    map.SetTransform(GeoTransform::NorthUp(40.0, 10.0, step));
    RasterAlbedoOptions plain;
    plain.detail.enabled = false;
    const RasterAlbedo smooth({map}, plain, moon);
    const RasterAlbedo detailed({map}, {}, moon);
    const double lon = 40.0 + 10.3 * step;
    const double lat = 10.0 - 12.7 * step;
    EXPECT_EQ(detailed.ValueAt(lon, lat, {100.0})[0], smooth.ValueAt(lon, lat, {100.0})[0]);
    EXPECT_NE(detailed.ValueAt(lon, lat, {2.0})[0], smooth.ValueAt(lon, lat, {2.0})[0]);
    const Albedo composed{std::make_shared<RasterAlbedo>(std::vector<Raster<float>>{map},
                                                         RasterAlbedoOptions{}, moon),
                          {},
                          moon};
    const RasterGrid grid{40, 40,
                          GeoTransform::NorthUp(40.0 + 3.0 * step, 10.0 - 3.0 * step, step / 8.0)};
    const auto bands = EvaluateAlbedo(composed, grid, {0.0, 2});
    const auto center = bands[0].CellCenter(17, 23);
    EXPECT_EQ(EvaluateAlbedoAt(composed, center.x, center.y, {100.0 / 8.0})[0], bands[0](17, 23));
}

TEST(RasterAlbedoStatistics, MeasureTheMapsLogBrightness) {
    terraingen::Raster<float> red(3, 2, 1.0f);
    terraingen::Raster<float> green(3, 2, 3.0f);
    green.SetNoData(0.0f);
    green(2, 1) = 0.0f;
    const std::vector<double> logs = terraingen::LogBrightnessGrid({red, green});
    ASSERT_EQ(logs.size(), 6u);
    EXPECT_DOUBLE_EQ(logs[0], std::log(2.0));
    EXPECT_TRUE(std::isnan(logs[5]));

    const std::vector<double> ramp{0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    EXPECT_DOUBLE_EQ(*terraingen::StructureFunction(ramp, 3, 2, 1), (4.0 + 3.0 * 9.0) / 7.0);
    EXPECT_FALSE(terraingen::StructureFunction(ramp, 3, 2, 3)) << "no pairs that far apart";
    EXPECT_FALSE(terraingen::StructureFunction(std::vector<double>(6, 1.0), 3, 2, 1))
        << "a uniform map";
    EXPECT_DOUBLE_EQ(terraingen::FitSlope({0.0, 1.0, 2.0}, {1.0, 3.0, 5.0}), 2.0);
}

TEST(ForEachAlbedoBlock, PreparesEachBlockOnceAndVisitsItsCells) {
    std::vector<terraingen::Raster<float>> bands{terraingen::Raster<float>(4, 70, 1.0f)};
    std::atomic<int> blocks = 0;
    terraingen::ForEachAlbedoBlock(bands, 2, 32, [&](std::size_t rowBegin, std::size_t rowEnd) {
        ++blocks;
        const auto offset = static_cast<float>(rowEnd - rowBegin);
        return terraingen::AlbedoCellVisitor(
            [offset](AlbedoSample& value, double, double) { value[0] += offset; });
    });
    EXPECT_EQ(blocks, 3);
    EXPECT_EQ(bands[0](0, 0), 33.0f);
    EXPECT_EQ(bands[0](3, 69), 7.0f) << "the last block holds 6 rows";
    std::vector<terraingen::Raster<float>> none;
    terraingen::ForEachAlbedoBlock(none, 1, 32, [](std::size_t, std::size_t) {
        ADD_FAILURE();
        return terraingen::AlbedoCellVisitor();
    });
}

TEST(ForEachAlbedoPixel, VisitsEveryCellByColumnAndRow) {
    std::vector<terraingen::Raster<float>> bands{terraingen::Raster<float>(3, 5, 0.0f),
                                                 terraingen::Raster<float>(3, 5, 1.0f)};
    terraingen::ForEachAlbedoPixel(bands, 2,
                                   [](AlbedoSample& value, std::size_t col, std::size_t row) {
                                       value[0] = static_cast<float>(10 * row + col);
                                       value[1] += 1.0f;
                                   });
    EXPECT_EQ(bands[0](2, 4), 42.0f);
    EXPECT_EQ(bands[0](1, 0), 1.0f);
    EXPECT_EQ(bands[1](0, 3), 2.0f);
    std::vector<terraingen::Raster<float>> none;
    terraingen::ForEachAlbedoPixel(none, 1,
                                   [](AlbedoSample&, std::size_t, std::size_t) { ADD_FAILURE(); });
}
