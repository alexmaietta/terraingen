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
