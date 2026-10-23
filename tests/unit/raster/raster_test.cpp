#include "terraingen/raster/raster.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

using terraingen::GeoTransform;
using terraingen::Raster;

TEST(Raster, IsMissingForNaNAndTheNoDataValue) {
    Raster<float> raster(1, 1);
    EXPECT_TRUE(raster.IsMissing(std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(raster.IsMissing(-9999.0f));
    raster.SetNoData(-9999.0f);
    EXPECT_TRUE(raster.IsMissing(-9999.0f));
    EXPECT_FALSE(raster.IsMissing(1.0f));
    Raster<int> counts(1, 1);
    counts.SetNoData(-1);
    EXPECT_TRUE(counts.IsMissing(-1));
    EXPECT_FALSE(counts.IsMissing(0));
}

TEST(Raster, DefaultIsEmpty) {
    const Raster<float> raster;
    EXPECT_TRUE(raster.Empty());
    EXPECT_EQ(raster.Width(), 0u);
    EXPECT_EQ(raster.Height(), 0u);
    EXPECT_FALSE(raster.NoData().has_value());
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
    const auto center = raster.CellCenter(1, 2);
    EXPECT_DOUBLE_EQ(center.x, 103.0);
    EXPECT_DOUBLE_EQ(center.y, 195.0);
}

TEST(Raster, IsNoDataMatchesTheValueAndAnyNanForANanNoData) {
    Raster<float> raster(1, 1);
    EXPECT_FALSE(raster.IsNoData(0.0f));
    raster.SetNoData(-1.0f);
    EXPECT_TRUE(raster.IsNoData(-1.0f));
    EXPECT_FALSE(raster.IsNoData(std::nanf("")));
    raster.SetNoData(std::nanf(""));
    EXPECT_TRUE(raster.IsNoData(std::nanf("")));
    EXPECT_FALSE(raster.IsNoData(-1.0f));
}

TEST(Raster, KeepsNoData) {
    Raster<float> raster(2, 2);
    raster.SetNoData(-9999.0f);
    ASSERT_TRUE(raster.NoData().has_value());
    EXPECT_EQ(*raster.NoData(), -9999.0f);
    raster.SetNoData(std::nullopt);
    EXPECT_FALSE(raster.NoData().has_value());
}
