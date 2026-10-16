#include "terraingen/raster/resample.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::Resample;
using terraingen::ResampleMethod;

namespace {

/// A `width` x `height` raster of 2 m cells with value a + b x + c y at each cell centre.
Raster<float> Plane(std::size_t width, std::size_t height, double a, double b, double c) {
    Raster<float> raster(width, height);
    raster.SetTransform(GeoTransform::NorthUp(-10.0, 20.0, 2.0));
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            const auto p = raster.CellCenter(col, row);
            raster(col, row) = static_cast<float>(a + b * p.x + c * p.y);
        }
    }
    return raster;
}

}  // namespace

TEST(Resample, KeepsTheExtentAndDirectionWithTheNewCellSize) {
    const Raster<float> source = Plane(10, 6, 0.0, 1.0, 0.0);
    const Raster<float> fine = Resample(source, 0.5, ResampleMethod::Bilinear);
    EXPECT_EQ(fine.Width(), 40u);
    EXPECT_EQ(fine.Height(), 24u);
    EXPECT_EQ(fine.Transform(), GeoTransform::NorthUp(-10.0, 20.0, 0.5));
    const Raster<float> coarse = Resample(source, 3.0, ResampleMethod::Nearest);
    EXPECT_EQ(coarse.Width(), 7u);
    EXPECT_EQ(coarse.Height(), 4u);
    EXPECT_EQ(Resample(source, 1000.0, ResampleMethod::Cubic).Width(), 1u);
}

TEST(Resample, BilinearAndCubicReproduceLinearSurfacesAwayFromTheEdge) {
    const Raster<float> source = Plane(16, 12, 5.0, 0.3, -0.7);
    for (const ResampleMethod method : {ResampleMethod::Bilinear, ResampleMethod::Cubic}) {
        const Raster<float> fine = Resample(source, 0.5, method);
        for (std::size_t row = 8; row + 8 < fine.Height(); ++row) {
            for (std::size_t col = 8; col + 8 < fine.Width(); ++col) {
                const auto p = fine.CellCenter(col, row);
                EXPECT_NEAR(fine(col, row), 5.0 + 0.3 * p.x - 0.7 * p.y, 1e-4) << col << " " << row;
            }
        }
    }
}

TEST(Resample, CubicIsCloserThanBilinearOnASmoothCurve) {
    Raster<float> source(32, 32);
    source.SetTransform(GeoTransform::NorthUp(0.0, 32.0, 1.0));
    const auto curve = [](double x, double y) { return std::sin(0.3 * x) * std::cos(0.2 * y); };
    for (std::size_t row = 0; row < 32; ++row) {
        for (std::size_t col = 0; col < 32; ++col) {
            const auto p = source.CellCenter(col, row);
            source(col, row) = static_cast<float>(curve(p.x, p.y));
        }
    }
    double errors[2] = {0.0, 0.0};
    for (int m = 0; m < 2; ++m) {
        const Raster<float> fine =
            Resample(source, 0.25, m == 0 ? ResampleMethod::Bilinear : ResampleMethod::Cubic);
        for (std::size_t row = 16; row + 16 < fine.Height(); ++row) {
            for (std::size_t col = 16; col + 16 < fine.Width(); ++col) {
                const auto p = fine.CellCenter(col, row);
                errors[m] = std::fmax(
                    errors[m], std::fabs(static_cast<double>(fine(col, row)) - curve(p.x, p.y)));
            }
        }
    }
    EXPECT_LT(errors[1], 0.5 * errors[0]);
}

TEST(Resample, NearestCopiesCellsAndNoDataPropagates) {
    Raster<float> source = Plane(4, 4, 0.0, 1.0, 10.0);
    source.SetNoData(-9999.0f);
    source(1, 1) = -9999.0f;
    const Raster<float> nearest = Resample(source, 1.0, ResampleMethod::Nearest);
    EXPECT_EQ(nearest(0, 0), source(0, 0));
    EXPECT_EQ(nearest(7, 7), source(3, 3));
    EXPECT_EQ(nearest(2, 2), -9999.0f);
    const Raster<float> bilinear = Resample(source, 1.0, ResampleMethod::Bilinear);
    EXPECT_EQ(bilinear.NoData(), source.NoData());
    EXPECT_EQ(bilinear(2, 2), -9999.0f);
    EXPECT_EQ(bilinear(3, 3), -9999.0f);
    EXPECT_NE(bilinear(7, 7), -9999.0f);
    EXPECT_EQ(Resample(source, 2.0, ResampleMethod::Cubic)(1, 1), -9999.0f);
}

TEST(Resample, RejectsInvalidInputs) {
    const Raster<float> source = Plane(4, 4, 0.0, 1.0, 0.0);
    EXPECT_THROW(Resample(Raster<float>(), 1.0, ResampleMethod::Nearest), std::invalid_argument);
    EXPECT_THROW(Resample(source, 0.0, ResampleMethod::Nearest), std::invalid_argument);
    EXPECT_THROW(Resample(source, std::numeric_limits<double>::infinity(), ResampleMethod::Nearest),
                 std::invalid_argument);
    EXPECT_THROW(Resample(source, 1e-12, ResampleMethod::Nearest), std::invalid_argument);
    Raster<float> rotated = source;
    rotated.SetTransform(GeoTransform{0.0, 1.0, 0.1, 0.0, 0.0, -1.0});
    EXPECT_THROW(Resample(rotated, 1.0, ResampleMethod::Nearest), std::invalid_argument);
}

TEST(SampleRaster, InterpolatesAtWorldPositions) {
    Raster<float> raster(4, 3);
    raster.SetTransform(GeoTransform::NorthUp(10.0, 30.0, 2.0));
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            raster(col, row) = static_cast<float>(col * 10 + row);
        }
    }
    EXPECT_EQ(terraingen::SampleRaster(raster, 13.0, 27.0, ResampleMethod::Bilinear), 11.0f);
    EXPECT_FLOAT_EQ(terraingen::SampleRaster(raster, 14.0, 27.0, ResampleMethod::Bilinear), 16.0f);
    EXPECT_EQ(terraingen::SampleRaster(raster, 14.1, 26.9, ResampleMethod::Nearest), 21.0f);
    EXPECT_TRUE(std::isnan(terraingen::SampleRaster(raster, 9.0, 27.0, ResampleMethod::Cubic)));
    EXPECT_TRUE(std::isnan(terraingen::SampleRaster(raster, 13.0, 31.0, ResampleMethod::Cubic)));
    raster.SetNoData(-1.0f);
    raster(1, 1) = -1.0f;
    EXPECT_TRUE(std::isnan(terraingen::SampleRaster(raster, 13.0, 27.0, ResampleMethod::Nearest)));
    EXPECT_TRUE(std::isnan(terraingen::SampleRaster(raster, 14.0, 27.0, ResampleMethod::Bilinear)));
}

TEST(ResampleTo, SamplesEachCellCentreAndMarksCellsBeyondTheRaster) {
    Raster<float> raster(4, 4, 5.0f);
    raster.SetTransform(GeoTransform::NorthUp(0.0, 8.0, 2.0));
    const terraingen::RasterGrid grid{6, 6, GeoTransform::NorthUp(-2.0, 10.0, 2.0)};
    const Raster<float> resampled = terraingen::ResampleTo(raster, grid, ResampleMethod::Cubic);
    EXPECT_EQ(terraingen::RasterGrid::Of(resampled), grid);
    EXPECT_FLOAT_EQ(resampled(2, 2), 5.0f);
    EXPECT_TRUE(resampled.IsNoData(resampled(0, 0)));
    EXPECT_TRUE(std::isnan(resampled(5, 3)));
    EXPECT_THROW(terraingen::ResampleTo(raster, terraingen::RasterGrid{}, ResampleMethod::Cubic),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::ResampleTo(Raster<float>(), grid, ResampleMethod::Cubic),
                 std::invalid_argument);
    raster.SetTransform(GeoTransform{0.0, 2.0, 0.3, 8.0, 0.3, -2.0});
    EXPECT_THROW(terraingen::SampleRaster(raster, 1.0, 1.0, ResampleMethod::Cubic),
                 std::invalid_argument);
}

TEST(FilledNoData, KeepsTheSourceValueOrMarksMissingCellsNan) {
    EXPECT_EQ(terraingen::FilledNoData(-5.0f, true), -5.0f);
    EXPECT_FALSE(terraingen::FilledNoData(std::nullopt, false).has_value());
    EXPECT_TRUE(std::isnan(*terraingen::FilledNoData(std::nullopt, true)));
}

TEST(SampleAtCentres, CallsTheSamplerAtEachCellCentre) {
    Raster<float> target(3, 2);
    target.SetTransform(GeoTransform::NorthUp(0.0, 2.0, 1.0));
    terraingen::SampleAtCentres(target, std::nullopt, [](double x, double y) {
        return x > 2.0 ? std::numeric_limits<float>::quiet_NaN() : static_cast<float>(x + 10 * y);
    });
    EXPECT_FLOAT_EQ(target(0, 0), 15.5f);
    EXPECT_FLOAT_EQ(target(1, 1), 6.5f);
    EXPECT_TRUE(std::isnan(target(2, 0)));
    EXPECT_TRUE(target.NoData().has_value());
    terraingen::SampleAtCentres(target, -1.0f, [](double, double) { return 1.0f; });
    EXPECT_EQ(target(2, 0), 1.0f);
    EXPECT_EQ(target.NoData(), -1.0f);
}
