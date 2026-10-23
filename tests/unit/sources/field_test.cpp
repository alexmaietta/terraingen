// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/field.h"

#include "support/temp_path.h"
#include "terraingen/raster/geotiff/geotiff.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::ConstantField;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterField;
using terraingen::RasterFieldOptions;
using terraingen::ResampleMethod;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// A 4 x 2 raster of 10 m cells from x = 0 and y = 20 down, holding 10 col + row, with one NaN
/// and one no-data cell.
Raster<float> Cells() {
    Raster<float> raster(4, 2);
    raster.SetTransform(GeoTransform::NorthUp(0.0, 20.0, 10.0));
    for (std::size_t row = 0; row < 2; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            raster(col, row) = static_cast<float>(10 * col + row);
        }
    }
    raster(3, 0) = std::numeric_limits<float>::quiet_NaN();
    raster(3, 1) = -9999.0f;
    raster.SetNoData(-9999.0f);
    return raster;
}

/// The default options with `method`.
RasterFieldOptions WithMethod(ResampleMethod method) {
    RasterFieldOptions options;
    options.method = method;
    return options;
}

/// The default options with `scale`.
RasterFieldOptions WithScale(double scale) {
    RasterFieldOptions options;
    options.scale = scale;
    return options;
}

/// The default options with `fallback`.
RasterFieldOptions WithFallback(double fallback) {
    RasterFieldOptions options;
    options.fallback = fallback;
    return options;
}

}  // namespace

TEST(ConstantField, IsTheSameEverywhere) {
    const ConstantField field(2.5);
    EXPECT_EQ(field.ValueAt(-1e6, 3.0), 2.5);
    EXPECT_EQ(field.Max(), 2.5);
    EXPECT_TRUE(std::isnan(ConstantField(kNaN).ValueAt(0.0, 0.0)));
}

TEST(MinimumField, TakesTheSmallerValueAndIgnoresNaN) {
    const auto five = std::make_shared<ConstantField>(5.0);
    const auto none = std::make_shared<ConstantField>(kNaN);
    const auto cells = std::make_shared<RasterField>(Cells());
    const terraingen::MinimumField both(five, cells);
    EXPECT_EQ(both.ValueAt(5.0, 15.0), 0.0);
    EXPECT_EQ(both.ValueAt(25.0, 15.0), 5.0);
    EXPECT_EQ(both.ValueAt(-100.0, 15.0), 5.0) << "the raster has no value there";
    EXPECT_EQ(both.Max(), 21.0);
    const terraingen::MinimumField unset(none, none);
    EXPECT_TRUE(std::isnan(unset.ValueAt(0.0, 0.0)));
    EXPECT_TRUE(std::isnan(unset.Max()));
    EXPECT_EQ(terraingen::MinimumField(none, five).Max(), 5.0);
    EXPECT_THROW(terraingen::MinimumField(nullptr, five), std::invalid_argument);
    EXPECT_THROW(terraingen::MinimumField(five, nullptr), std::invalid_argument);
}

TEST(RasterField, InterpolatesItsCellsAndBoundsThem) {
    const RasterField field(Cells(), WithScale(2.0));
    EXPECT_DOUBLE_EQ(field.ValueAt(5.0, 15.0), 0.0);
    EXPECT_DOUBLE_EQ(field.ValueAt(15.0, 15.0), 20.0);
    EXPECT_DOUBLE_EQ(field.ValueAt(10.0, 15.0), 10.0) << "bilinear between cell centers";
    EXPECT_DOUBLE_EQ(field.ValueAt(15.0, 10.0), 21.0);
    EXPECT_DOUBLE_EQ(field.Max(), 42.0) << "the NaN and no-data cells are left out";
    EXPECT_TRUE(std::isnan(field.ValueAt(35.0, 15.0)));
    EXPECT_DOUBLE_EQ(field.ValueAt(30.0, 15.0), 40.0) << "a NaN cell only removes itself";
    EXPECT_DOUBLE_EQ(field.ValueAt(30.0, 10.0), 41.0) << "and a no-data cell";
    EXPECT_TRUE(std::isnan(field.ValueAt(-5.0, 15.0))) << "outside";
    const RasterField nearest(Cells(), WithMethod(ResampleMethod::Nearest));
    EXPECT_DOUBLE_EQ(nearest.ValueAt(11.0, 19.0), 10.0);
    EXPECT_EQ(nearest.Values().Width(), 4u);
    EXPECT_EQ(nearest.Options().method, ResampleMethod::Nearest);
}

TEST(RasterField, FallsBackWhereItHasNoValue) {
    const RasterField field(Cells(), WithFallback(99.0));
    EXPECT_EQ(field.ValueAt(-5.0, 15.0), 99.0);
    EXPECT_EQ(field.ValueAt(35.0, 5.0), 99.0);
    EXPECT_EQ(field.Max(), 99.0);
    EXPECT_EQ(RasterField(Cells(), WithFallback(-1.0)).Max(), 21.0);
}

TEST(RasterField, WrapsLongitudes) {
    Raster<float> raster(4, 2, 1.0f);
    raster.SetTransform(GeoTransform::NorthUp(0.0, 10.0, 90.0));
    raster(3, 0) = 7.0f;
    raster(3, 1) = 7.0f;
    const RasterField plain(raster, WithMethod(ResampleMethod::Nearest));
    RasterFieldOptions periodic = WithMethod(ResampleMethod::Nearest);
    periodic.periodicLongitude = true;
    const RasterField wrapped(raster, periodic);
    EXPECT_TRUE(std::isnan(plain.ValueAt(-45.0, 0.0)));
    EXPECT_EQ(wrapped.ValueAt(-45.0, 0.0), 7.0) << "-45 deg reads 315 deg";
    EXPECT_EQ(wrapped.ValueAt(405.0, 0.0), 1.0) << "405 deg reads 45 deg";
}

TEST(RasterField, RejectsWhatItCannotRead) {
    EXPECT_THROW(RasterField(Raster<float>{}), std::invalid_argument);
    EXPECT_THROW(RasterField(Cells(), WithMethod(ResampleMethod::Cubic)), std::invalid_argument);
    EXPECT_THROW(RasterField(Cells(), WithScale(0.0)), std::invalid_argument);
    Raster<float> rotated = Cells();
    GeoTransform transform = rotated.Transform();
    transform.rowRotation = 0.1;
    rotated.SetTransform(transform);
    EXPECT_THROW(RasterField{rotated}, std::invalid_argument);
}

TEST(ReadRasterField, ReadsAGeoTiffBand) {
    const auto path = terraingen::testing::TempPath("raster_field.tif");
    const terraingen::GeoKeys keys{{1, 1, 0, 1, 1024, 0, 1, 1}, {}, {}};
    terraingen::WriteGeoTiff(path, Cells(), keys);
    const auto field = terraingen::ReadRasterField(path, WithScale(0.5));
    EXPECT_DOUBLE_EQ(field->ValueAt(15.0, 15.0), 5.0);
    EXPECT_DOUBLE_EQ(field->Max(), 10.5);
}
