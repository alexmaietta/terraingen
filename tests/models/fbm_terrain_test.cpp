#include "terraingen/models/fbm_terrain.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

using terraingen::FbmParams;
using terraingen::FillFbm;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::ValidateFbmParams;

TEST(ValidateFbmParams, AcceptsDefaults) {
    EXPECT_NO_THROW(ValidateFbmParams(FbmParams{}));
}

TEST(ValidateFbmParams, RejectsInvalidFields) {
    const auto rejects = [](auto mutate) {
        FbmParams params;
        mutate(params);
        EXPECT_THROW(ValidateFbmParams(params), std::invalid_argument);
    };
    rejects([](FbmParams& p) { p.wavelength = 0.0f; });
    rejects([](FbmParams& p) { p.wavelength = std::numeric_limits<float>::infinity(); });
    rejects([](FbmParams& p) { p.amplitude = std::numeric_limits<float>::quiet_NaN(); });
    rejects([](FbmParams& p) { p.octaves = 0; });
    rejects([](FbmParams& p) { p.lacunarity = 1.0f; });
    rejects([](FbmParams& p) { p.lacunarity = std::numeric_limits<float>::infinity(); });
    rejects([](FbmParams& p) { p.gain = 0.0f; });
    rejects([](FbmParams& p) { p.gain = std::numeric_limits<float>::quiet_NaN(); });
}

TEST(FillFbm, WritesFbmAtCellCentres) {
    Raster<float> raster(8, 4);
    raster.SetTransform(GeoTransform::NorthUp(100.0, 50.0, 2.0));
    const FbmParams params;
    FillFbm(raster, params);
    const auto centre = raster.CellCenter(3, 2);
    EXPECT_EQ(raster(3, 2),
              terraingen::Fbm2(static_cast<float>(centre.x), static_cast<float>(centre.y), params));
}

TEST(FillFbm, TilesMatchTheWholeRaster) {
    FbmParams params;
    params.wavelength = 40.0f;
    params.seed = 7u;

    constexpr std::size_t kSize = 64;
    constexpr std::size_t kTile = 16;
    constexpr double kPixel = 0.5;
    Raster<float> whole(kSize, kSize);
    whole.SetTransform(GeoTransform::NorthUp(-1000.0, 2000.0, kPixel));
    FillFbm(whole, params);

    for (std::size_t tileRow = 0; tileRow < kSize / kTile; ++tileRow) {
        for (std::size_t tileCol = 0; tileCol < kSize / kTile; ++tileCol) {
            Raster<float> tile(kTile, kTile);
            tile.SetTransform(GeoTransform::NorthUp(
                -1000.0 + static_cast<double>(tileCol * kTile) * kPixel,
                2000.0 - static_cast<double>(tileRow * kTile) * kPixel, kPixel));
            FillFbm(tile, params);
            for (std::size_t row = 0; row < kTile; ++row) {
                for (std::size_t col = 0; col < kTile; ++col) {
                    ASSERT_EQ(tile(col, row), whole(tileCol * kTile + col, tileRow * kTile + row));
                }
            }
        }
    }
}

TEST(FillFbm, RejectsInvalidParams) {
    Raster<float> raster(2, 2);
    FbmParams params;
    params.octaves = 0;
    EXPECT_THROW(FillFbm(raster, params), std::invalid_argument);
}
