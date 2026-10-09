#include "terraingen/app/viewer/terrain_scene.h"

#include <ernie/scene/tokens.h>
#include <ernie/util/tensor.h>
#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <limits>
#include <stdexcept>
#include <vector>

namespace {

namespace tokens = ernie::tokens;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::viewer::kMaterialPrim;
using terraingen::viewer::kSettingsPrim;
using terraingen::viewer::kSunPrim;
using terraingen::viewer::kTerrainPrim;
using terraingen::viewer::SetUpScene;
using terraingen::viewer::ShowTerrain;

Raster<float> Ramp(std::size_t width, std::size_t height, double cell = 2.0) {
    Raster<float> raster(width, height);
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            raster(col, row) = static_cast<float>(col + 10 * row);
        }
    }
    raster.SetTransform(GeoTransform::NorthUp(100.0, 500.0, cell));
    return raster;
}

}  // namespace

TEST(TerrainScene, SetUpKeepsPrimsAlreadyThere) {
    ernie::Scene scene;
    scene.Define(kSunPrim, tokens::kDistantLight).Set(tokens::kAngle, 2.0);
    SetUpScene(scene);
    EXPECT_EQ(scene.TypeOf(kMaterialPrim), tokens::kMaterial);
    EXPECT_EQ(scene.TypeOf(kSettingsPrim), tokens::kRenderSettings);
    EXPECT_EQ(scene.Get<double>(kSunPrim, tokens::kAngle, 0.0), 2.0);
}

TEST(TerrainScene, WritesHeightsWhereTheRasterLiesWithItsNoDataAndAlbedo) {
    ernie::Scene scene;
    SetUpScene(scene);
    Raster<float> heights = Ramp(5, 3);
    heights.SetNoData(-9999.0f);
    const Raster<float> albedo = Ramp(5, 3);
    ShowTerrain(scene, heights, false, &albedo);

    const ernie::Tensor* written = scene.Read(kTerrainPrim, tokens::kHeights);
    ASSERT_NE(written, nullptr);
    EXPECT_EQ(written->Shape(), (std::vector<std::int64_t>{3, 5}));
    EXPECT_EQ(written->As<float>()[5 * 2 + 4], 24.0f);
    EXPECT_EQ(scene.Get<glm::dvec2>(kTerrainPrim, tokens::kOrigin, {}), glm::dvec2(100.0, 500.0));
    EXPECT_EQ(scene.Get<double>(kTerrainPrim, tokens::kCellSize, 0.0), 2.0);
    EXPECT_EQ(scene.Get<double>(kTerrainPrim, tokens::kNoData, 0.0), -9999.0);
    EXPECT_NE(scene.Read(kMaterialPrim, tokens::kAlbedoMap), nullptr);
    EXPECT_EQ(scene.Get<double>(kMaterialPrim, tokens::kAlbedoMapCellSize, 0.0), 2.0);

    ShowTerrain(scene, Ramp(4, 4));
    EXPECT_EQ(scene.Read(kTerrainPrim, tokens::kHeights)->Shape(),
              (std::vector<std::int64_t>{4, 4}));
    EXPECT_EQ(scene.Get(kTerrainPrim, tokens::kNoData), nullptr);
    EXPECT_EQ(scene.Read(kMaterialPrim, tokens::kAlbedoMap), nullptr) << "the old map goes";
}

TEST(TerrainScene, RejectsRastersItCannotPlace) {
    ernie::Scene scene;
    SetUpScene(scene);
    EXPECT_THROW(ShowTerrain(scene, Ramp(1, 4)), std::invalid_argument);
    EXPECT_THROW(ShowTerrain(scene, Ramp(4, 4), true), std::invalid_argument);
    Raster<float> skewed = Ramp(4, 4);
    skewed.SetTransform({0.0, 1.0, 0.2, 0.0, 0.0, -1.0});
    EXPECT_THROW(ShowTerrain(scene, skewed), std::invalid_argument);
    Raster<float> oblong = Ramp(4, 4);
    oblong.SetTransform({0.0, 1.0, 0.0, 0.0, 0.0, -2.0});
    EXPECT_THROW(ShowTerrain(scene, oblong), std::invalid_argument);
    Raster<float> southUp = Ramp(4, 4);
    southUp.SetTransform({0.0, 1.0, 0.0, 0.0, 0.0, 1.0});
    EXPECT_THROW(ShowTerrain(scene, southUp), std::invalid_argument);
    const Raster<float> elsewhere = Ramp(4, 4, 3.0);
    EXPECT_THROW(ShowTerrain(scene, Ramp(4, 4), false, &elsewhere), std::invalid_argument);
    const Raster<float> smaller = Ramp(3, 4);
    EXPECT_THROW(ShowTerrain(scene, Ramp(4, 4), false, &smaller), std::invalid_argument);
}
