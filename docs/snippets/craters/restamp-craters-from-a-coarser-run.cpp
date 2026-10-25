// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Restamp Craters from a Coarser Run
///
/// Running a terrain coarse first and then finer on its heights blurs the coarse run's craters:
/// the finer run upsamples them between posts. Inherited craters read from the coarse run's crater
/// list are stamped again sharp, with their IDs, ages and realized depths, where the source
/// resolves them, replacing what it holds; generated craters fill the gaps beneath, so none is
/// drawn twice. Here a run at 8 m hands its craters from 32 m across to a run at 1 m.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>
#include <utility>

namespace terraingen {
namespace {

TEST_F(SnippetTest, RestampCratersFromACoarserRun) {
    const auto Craters = [](InheritedCraters inherited = {}, TerrainBase base = {}) {
        CraterLayerOptions options;  // the same layer at every level: 4 m to 120 m, seed 5
        options.seed = 5;
        options.generation.dMin = 4.0;
        options.generation.dMax = 120.0;
        options.sourceResolvesPixels = 4.0;  // the DEM holds craters from 4 x 8 m = 32 m across
        options.inherited = std::move(inherited);
        return std::make_shared<CraterLayer>(DefaultCraterModels(), options, std::move(base));
    };
    const std::filesystem::path list = OutputFolder() / "coarse_craters.json";
    TerrainGenerator coarse;
    coarse.source = std::make_shared<FlatSource>(75, 75, 8.0);
    coarse.layers.push_back(Craters());
    const CraterFeatures placed = PlacedCraters(coarse, RasterGrid::FromRaster(Evaluate(coarse)));
    WriteCraterList(placed.craters, list, ListFormat::Json, placed.attributes);
    const auto dem = std::make_shared<RasterSource>(Evaluate(coarse));
    // --8<-- [start:restamp_craters_from_a_coarser_run]
    InheritedCraters inherited;
    inherited.craters = ReadCraterList(list);  // with the coarse run's realized depths
    const auto craters = Craters(inherited, TerrainBase{Surface::Plane(), dem});
    TerrainGenerator fine;
    fine.source = dem;
    fine.layers.push_back(craters);
    const Raster<float> heights =
        Evaluate(fine, RasterGrid::Centered({0.0, 0.0}, {200.0, 200.0}, 1.0));
    // --8<-- [end:restamp_craters_from_a_coarser_run]
    const CraterList applied = craters->Craters({-100.0, -100.0, 100.0, 100.0});
    std::size_t restamped = 0;
    for (std::size_t n = 0; n < applied.Size(); ++n) {
        const Crater crater = applied.At(n);
        restamped += crater.replace ? 1u : 0u;
        EXPECT_EQ(crater.replace, crater.diameter >= 32.0) << crater.diameter;
    }
    EXPECT_GT(restamped, 0u);
    EXPECT_EQ(heights.Width(), 200u);
}

}  // namespace
}  // namespace terraingen
