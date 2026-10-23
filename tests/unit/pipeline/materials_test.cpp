#include "terraingen/pipeline/materials.h"

#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/dunes.h"
#include "terraingen/layers/sand_fill.h"
#include "terraingen/layers/sand_shadow.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <vector>

using terraingen::MaterialClass;
using terraingen::MaterialOptions;
using terraingen::MaterialsAt;
using terraingen::TerrainGenerator;

namespace {

std::size_t Index(MaterialClass material) {
    return static_cast<std::size_t>(material);
}

/// Ground tilted along x by `slope`, with a Gaussian pit of `depth` at (`pitX`, 0).
class Tilt final : public terraingen::FormulaLayer {
  public:
    Tilt(double slope, double depth, double pitX) : slope_(slope), depth_(depth), pitX_(pitX) {}

  protected:
    float HeightAt(float height, double x, double y, double,
                   const terraingen::Ground*) const override {
        const double pit = depth_ * std::exp(-((x - pitX_) * (x - pitX_) + y * y) / 9.0);
        return static_cast<float>(static_cast<double>(height) + slope_ * x - pit);
    }

  private:
    double slope_;
    double depth_;
    double pitX_;
};

TerrainGenerator Ground(double slope, double depth = 0.0, double pitX = 0.0) {
    return TerrainGenerator{nullptr, {std::make_shared<Tilt>(slope, depth, pitX)}, {}};
}

}  // namespace

TEST(MaterialsAt, StartAsSoilWithBedrockOnSteepGround) {
    const MaterialOptions options;
    const auto level = MaterialsAt(Ground(0.0), options, 3.0, 4.0, 1.0);
    EXPECT_EQ(level[Index(MaterialClass::Soil)], 1.0);
    const auto steep = MaterialsAt(Ground(0.6), options, 3.0, 4.0, 1.0);
    EXPECT_EQ(steep[Index(MaterialClass::Bedrock)], 1.0);
    MaterialOptions noBedrock;
    noBedrock.exposure.reset();
    EXPECT_EQ(MaterialsAt(Ground(0.6), noBedrock, 3.0, 4.0, 1.0)[Index(MaterialClass::Soil)], 1.0);
    EXPECT_THROW(MaterialsAt(Ground(0.0), options, 0.0, 0.0, 0.0), std::invalid_argument);
}

TEST(MaterialsAt, LaysDustInPatchesAndSandFromLayersInOrder) {
    MaterialOptions options;
    options.dustCover = 0.4;
    options.dustPatch = 20.0;
    double dust = 0.0;
    int count = 0;
    for (double y = 0.0; y < 400.0; y += 4.0) {
        for (double x = 0.0; x < 400.0; x += 4.0) {
            dust += MaterialsAt(Ground(0.0), options, x, y, 1.0)[Index(MaterialClass::Dust)];
            ++count;
        }
    }
    EXPECT_NEAR(dust / count, 0.4, 0.15) << "about the dust cover";

    TerrainGenerator sandy = Ground(0.6, 3.0, 0.0);
    sandy.layers.push_back(std::make_shared<terraingen::SandFillLayer>(
        terraingen::SandFillOptions{8.0, 0.8, 1.0, 0.02}));
    const auto pit = MaterialsAt(sandy, {}, 0.0, 0.0, 0.5);
    EXPECT_EQ(pit[Index(MaterialClass::Sand)], 1.0) << "sand over the steep bedrock";
    const auto beside = MaterialsAt(sandy, {}, 40.0, 0.0, 0.5);
    EXPECT_EQ(beside[Index(MaterialClass::Sand)], 0.0);
    EXPECT_EQ(beside[Index(MaterialClass::Bedrock)], 1.0);
}

TEST(EvaluateMaterials, GivesEachCellItsSharesSummingToOne) {
    TerrainGenerator terrain = Ground(0.15, 2.0, 5.0);
    terraingen::DuneOptions dunes;
    dunes.shape = terraingen::DuneShape::Transverse;
    dunes.wavelength = 40.0;
    dunes.height = 3.0;
    terrain.layers.push_back(std::make_shared<terraingen::DuneLayer>(dunes));
    MaterialOptions options;
    options.dustCover = 0.3;
    options.dustPatch = 15.0;
    const terraingen::RasterGrid grid{24, 18, terraingen::GeoTransform::NorthUp(-30.0, 20.0, 2.0)};
    const auto bands = terraingen::EvaluateMaterials(terrain, options, grid, {0.0, 3});
    ASSERT_EQ(bands.size(), terraingen::kMaterialClasses);
    double sand = 0.0;
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const auto center = bands[0].CellCenter(col, row);
            const auto shares = MaterialsAt(terrain, options, center.x, center.y, 2.0);
            double sum = 0.0;
            for (std::size_t k = 0; k < terraingen::kMaterialClasses; ++k) {
                ASSERT_EQ(bands[k](col, row), static_cast<float>(shares[k]));
                sum += shares[k];
            }
            EXPECT_NEAR(sum, 1.0, 1e-12);
            sand += shares[Index(MaterialClass::Sand)];
        }
    }
    EXPECT_GT(sand, 0.0);
}

TEST(EvaluateMaterials, FindsBouldersOnceAndAgreesWithPoints) {
    TerrainGenerator terrain = Ground(0.05, 0.0, 0.0);
    terraingen::BoulderLayerOptions rocks;
    rocks.sizes.coverage = 0.1;
    rocks.sizes.minDiameter = 0.1;
    rocks.sizes.maxDiameter = 1.0;
    rocks.fillet.heightShare = 0.4;
    rocks.rendering.relief = terraingen::BoulderRelief::Shape;
    rocks.rendering.models.minDiameter = 0.6;
    const auto boulders = std::make_shared<terraingen::BoulderLayer>(rocks);
    terrain.layers.push_back(boulders);
    terraingen::SandShadowOptions tails;
    tails.lengthRatio = 2.0;
    terrain.layers.push_back(std::make_shared<terraingen::SandShadowLayer>(boulders, tails));
    const terraingen::RasterGrid grid{64, 48, terraingen::GeoTransform::NorthUp(-1.6, 1.2, 0.05)};
    const auto bands = terraingen::EvaluateMaterials(terrain, MaterialOptions{}, grid, {0.0, 3});
    double rock = 0.0;
    double sand = 0.0;
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const auto center = bands[0].CellCenter(col, row);
            const auto shares = MaterialsAt(terrain, MaterialOptions{}, center.x, center.y, 0.05);
            for (std::size_t k = 0; k < terraingen::kMaterialClasses; ++k) {
                ASSERT_EQ(bands[k](col, row), static_cast<float>(shares[k]));
            }
            rock += shares[Index(MaterialClass::Rock)];
            sand += shares[Index(MaterialClass::Sand)];
        }
    }
    EXPECT_GT(rock, 50.0);
    EXPECT_GT(sand, 20.0);
}

TEST(MaterialOptions, RejectsBadDustAndExposure) {
    MaterialOptions options;
    options.dustCover = 1.0;
    EXPECT_THROW(terraingen::ValidateMaterialOptions(options), std::invalid_argument);
    options.dustCover = 0.2;
    options.dustPatch = 0.0;
    EXPECT_THROW(terraingen::ValidateMaterialOptions(options), std::invalid_argument);
    options.dustPatch = 10.0;
    options.exposure->reach = -1.0;
    EXPECT_THROW(terraingen::ValidateMaterialOptions(options), std::invalid_argument);
}
