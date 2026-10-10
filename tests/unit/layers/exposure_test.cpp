#include "terraingen/layers/exposure.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <stdexcept>

using terraingen::ExposureField;
using terraingen::ExposureParams;

namespace {

/// A ground given by a function of position.
class Analytic final : public terraingen::Ground {
  public:
    explicit Analytic(std::function<float(double, double)> height) : height_(std::move(height)) {}

    float HeightAt(double x, double y, double /*spacing*/) const override {
        return height_(x, y);
    }

  private:
    std::function<float(double, double)> height_;
};

}  // namespace

TEST(ExposureField, CallsLevelGroundSoilAndSteepGroundBedrock) {
    const ExposureField field;
    EXPECT_EQ(field.Exposure(Analytic([](double, double) { return 3.0f; }), 0.0, 0.0, 1.0), 0.0);
    const auto plane = [](double slope) {
        return Analytic([slope](double x, double) { return static_cast<float>(slope * x); });
    };
    EXPECT_EQ(field.Exposure(plane(0.1), 5.0, 5.0, 1.0), 0.0);
    EXPECT_NEAR(field.Exposure(plane(0.3), 5.0, 5.0, 1.0), 0.5, 1e-5) << "halfway: smoothstep 0.5";
    EXPECT_EQ(field.Exposure(plane(0.6), 5.0, 5.0, 1.0), 1.0);
}

TEST(ExposureField, CallsRisesBedrockAndLowsSoil) {
    const ExposureField field;
    const Analytic mound([](double x, double y) {
        return static_cast<float>(2.0 * std::exp(-(x * x + y * y) / 200.0));
    });
    EXPECT_GT(field.Exposure(mound, 0.0, 0.0, 1.0), 0.99) << "a 2 m mound is bedrock on top";
    const Analytic pit([](double x, double y) {
        return static_cast<float>(-2.0 * std::exp(-(x * x + y * y) / 200.0));
    });
    EXPECT_EQ(field.Exposure(pit, 0.0, 0.0, 1.0), 0.0) << "soil settles in the low";
}

TEST(ExposureField, ReadsMissingSurroundingsAsLevelAndMissingPointsAsSoil) {
    const ExposureField field;
    const Analytic holes(
        [](double x, double) { return x > 5.0 ? std::nanf("") : static_cast<float>(0.6 * x); });
    EXPECT_EQ(field.Exposure(holes, 10.0, 0.0, 1.0), 0.0);
    EXPECT_GT(field.Exposure(holes, -20.0, 0.0, 1.0), 0.99);
}

TEST(ExposureField, CoversLevelGroundInPatchesWithFloorCover) {
    const Analytic flat([](double, double) { return 0.0f; });
    const ExposureField field({.floorCover = 0.3, .floorPatch = 20.0, .seed = 4});
    double bedrock = 0.0;
    int samples = 0;
    for (int j = 0; j < 100; ++j) {
        for (int i = 0; i < 100; ++i) {
            bedrock += field.Exposure(flat, 3.1 * i, 2.9 * j, 1.0);
            ++samples;
        }
    }
    EXPECT_NEAR(bedrock / samples, 0.3, 0.12);
    EXPECT_EQ(ExposureField().Exposure(flat, 7.0, 3.0, 1.0), 0.0);
}

TEST(ExposureField, FillsGridsWithThePointValues) {
    const ExposureField field;
    const Analytic ridge(
        [](double x, double) { return static_cast<float>(3.0 * std::cos(x / 15.0)); });
    terraingen::Raster<float> raster(20, 8);
    raster.SetTransform(terraingen::GeoTransform::NorthUp(-50.0, 10.0, 5.0));
    field.Fill(raster, ridge, 5.0, 3);
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            const auto centre = raster.CellCenter(col, row);
            ASSERT_EQ(raster(col, row),
                      static_cast<float>(field.Exposure(ridge, centre.x, centre.y, 5.0)));
        }
    }
}

TEST(ExposureField, RejectsInvalidParameters) {
    EXPECT_THROW(ExposureField({.reach = 0.0}), std::invalid_argument);
    EXPECT_THROW(ExposureField({.riseSoil = 0.05}), std::invalid_argument);
    EXPECT_THROW(ExposureField({.slopeBoulder = 0.1}), std::invalid_argument);
    EXPECT_THROW(ExposureField({.floorCover = 1.0}), std::invalid_argument);
    EXPECT_THROW(ExposureField({.floorPatch = -1.0}), std::invalid_argument);
    EXPECT_EQ(ExposureField().Params(), ExposureParams{});
}
