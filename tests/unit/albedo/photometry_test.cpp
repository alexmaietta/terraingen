#include "terraingen/albedo/photometry.h"

#include "terraingen/raster/grid.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

using terraingen::MaterialClass;

namespace {

std::size_t Index(MaterialClass material) {
    return static_cast<std::size_t>(material);
}

}  // namespace

TEST(Photometry, MixesPancamParametersByShare) {
    const auto pancam = terraingen::MarsPancamPhotometry();
    ASSERT_TRUE(pancam[Index(MaterialClass::Sand)]);
    EXPECT_EQ(pancam[Index(MaterialClass::Sand)]->albedo, 0.52);
    EXPECT_EQ(pancam[Index(MaterialClass::Soil)]->roughness, 15.0);
    EXPECT_EQ(pancam[Index(MaterialClass::Rock)]->b, 0.931);
    ASSERT_TRUE(pancam[Index(MaterialClass::Bedrock)]);
    EXPECT_EQ(*pancam[Index(MaterialClass::Bedrock)],
              (terraingen::HapkeParameters{0.90, 0.372, 0.202, 17.0}))
        << "Johnson et al. 2006b, Eagle crater outcrop at 753 nm";
    EXPECT_NO_THROW(terraingen::ValidateMaterialPhotometry(pancam));

    terraingen::MaterialShares shares{};
    shares[Index(MaterialClass::Sand)] = 0.25;
    shares[Index(MaterialClass::Dust)] = 0.75;
    const auto mixed = terraingen::MixPhotometry(shares, pancam);
    EXPECT_DOUBLE_EQ(mixed.albedo, 0.25 * 0.52 + 0.75 * 0.66);
    EXPECT_DOUBLE_EQ(mixed.roughness, 0.25 * 9.0 + 0.75 * 16.0);
    shares[Index(MaterialClass::Bedrock)] = 0.1;
    auto partial = pancam;
    partial[Index(MaterialClass::Bedrock)].reset();
    EXPECT_THROW(terraingen::MixPhotometry(shares, partial), std::invalid_argument);

    auto bad = pancam;
    bad[Index(MaterialClass::Dust)]->albedo = 1.5;
    EXPECT_THROW(terraingen::ValidateMaterialPhotometry(bad), std::invalid_argument);
    bad[Index(MaterialClass::Dust)] = terraingen::HapkeParameters{0.5, 0.5, 0.5, 95.0};
    EXPECT_THROW(terraingen::ValidateMaterialPhotometry(bad), std::invalid_argument);
}

TEST(Photometry, EvaluatesFourBandsFromMaterialRasters) {
    const terraingen::RasterGrid grid{4, 3, terraingen::GeoTransform::NorthUp(0.0, 3.0, 1.0)};
    std::vector<terraingen::Raster<float>> materials(terraingen::kMaterialClasses,
                                                     grid.MakeRaster());
    for (std::size_t i = 0; i < materials[0].Size(); ++i) {
        materials[Index(MaterialClass::Soil)].Values()[i] = 0.5f;
        materials[Index(MaterialClass::Sand)].Values()[i] = 0.5f;
    }
    const auto bands =
        terraingen::EvaluatePhotometry(materials, terraingen::MarsPancamPhotometry());
    ASSERT_EQ(bands.size(), 4u);
    EXPECT_FLOAT_EQ(bands[0](1, 1), static_cast<float>(0.5 * 0.76 + 0.5 * 0.52));
    EXPECT_FLOAT_EQ(bands[3](2, 0), 12.0f);
    EXPECT_THROW(terraingen::EvaluatePhotometry({}, terraingen::MarsPancamPhotometry()),
                 std::invalid_argument);
    materials[2] = terraingen::RasterGrid{2, 2, {}}.MakeRaster();
    EXPECT_THROW(terraingen::EvaluatePhotometry(materials, terraingen::MarsPancamPhotometry()),
                 std::invalid_argument);
}
