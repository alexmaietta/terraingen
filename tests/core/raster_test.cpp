#include "terraingen/core/raster.h"

#include <gtest/gtest.h>

#include <stdexcept>

using terraingen::GeoTransform;
using terraingen::Raster;

TEST(Raster, DefaultIsEmpty) {
    const Raster<float> raster;
    EXPECT_TRUE(raster.Empty());
    EXPECT_EQ(raster.Width(), 0u);
    EXPECT_EQ(raster.Height(), 0u);
    EXPECT_FALSE(raster.NoData().has_value());
    EXPECT_TRUE(raster.Crs().empty());
}

TEST(Raster, ConstructsFilled) {
    const Raster<float> raster(4, 3, 2.5f);
    EXPECT_EQ(raster.Width(), 4u);
    EXPECT_EQ(raster.Height(), 3u);
    EXPECT_EQ(raster.Size(), 12u);
    for (const float value : raster.Values()) {
        EXPECT_EQ(value, 2.5f);
    }
}

TEST(Raster, StoresCellsRowMajor) {
    Raster<int> raster(3, 2);
    raster(2, 0) = 7;
    raster(0, 1) = 9;
    EXPECT_EQ(raster.Values()[2], 7);
    EXPECT_EQ(raster.Values()[3], 9);
    EXPECT_EQ(raster.Row(1)[0], 9);
    EXPECT_EQ(raster.Row(1).size(), 3u);
}

TEST(Raster, AtChecksBounds) {
    Raster<float> raster(3, 2);
    raster.At(2, 1) = 1.0f;
    EXPECT_EQ(raster(2, 1), 1.0f);
    EXPECT_THROW(raster.At(3, 0), std::out_of_range);
    EXPECT_THROW(raster.At(0, 2), std::out_of_range);
}

TEST(Raster, CellCenterUsesTransform) {
    Raster<float> raster(10, 10);
    raster.SetTransform(GeoTransform::NorthUp(100.0, 200.0, 2.0));
    const auto centre = raster.CellCenter(1, 2);
    EXPECT_DOUBLE_EQ(centre.x, 103.0);
    EXPECT_DOUBLE_EQ(centre.y, 195.0);
}

TEST(Raster, KeepsGeoreferenceAndNoData) {
    Raster<float> raster(2, 2);
    raster.SetCrs("LOCAL_CS[\"test\"]");
    raster.SetNoData(-9999.0f);
    EXPECT_EQ(raster.Crs(), "LOCAL_CS[\"test\"]");
    ASSERT_TRUE(raster.NoData().has_value());
    EXPECT_EQ(*raster.NoData(), -9999.0f);
    raster.SetNoData(std::nullopt);
    EXPECT_FALSE(raster.NoData().has_value());
}
