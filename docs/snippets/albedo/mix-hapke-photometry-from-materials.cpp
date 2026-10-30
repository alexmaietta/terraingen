// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Mix Hapke Photometry From a Material Map
///
/// A material map gives each cell its share of soil, sand, dust, bedrock and rock, from bare steep
/// ground, dust patches and the layers that lay a material, here dune sand. Each class has Hapke
/// parameters, such as Pancam's fits for Mars at 753 nm, and `EvaluatePhotometry` mixes them by
/// the shares into rasters of the single-scattering albedo, the two phase function terms and the
/// roughness for a renderer.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, MixHapkePhotometryFromMaterials) {
    Fractal hills = Fractal::Fbm(5, 120.0, 12.0);
    hills.octaves = 6;
    DuneOptions dunes;
    dunes.shape = DuneShape::Transverse;
    dunes.wavelength = 30.0;
    dunes.height = 2.0;
    const std::vector<LayerPtr> layers = {std::make_shared<NoiseLayer>(hills),
                                          std::make_shared<DuneLayer>(dunes)};
    const Terrain terrain(Surface{}, std::make_shared<FlatSource>(201, 201, 1.0), layers);
    const RasterGrid grid = RasterGrid::FromRaster(terrain.Evaluate());
    // --8<-- [start:mix_hapke_photometry_from_materials]
    MaterialOptions options;
    options.dustCover = 0.3;
    options.dustPatch = 40.0;  // meters
    options.seed = 1;
    const std::vector<Raster<float>> materials = EvaluateMaterials(terrain, options, grid);
    const MaterialPhotometry photometry = MarsPancamPhotometry();  // per material at 753 nm
    // Single-scattering albedo, b, c and roughness.
    const std::vector<Raster<float>> hapke = EvaluatePhotometry(materials, photometry);
    // --8<-- [end:mix_hapke_photometry_from_materials]
    ASSERT_EQ(materials.size(), kMaterialClasses);
    ASSERT_EQ(hapke.size(), 4u);
    EXPECT_GT(hapke[0](100, 100), 0.0f);
    EXPECT_LE(hapke[0](100, 100), 1.0f);
}

}  // namespace
}  // namespace terraingen
