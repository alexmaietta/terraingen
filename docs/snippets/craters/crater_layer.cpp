// SPDX-License-Identifier: BSD-3-Clause

// Crater layer snippets for the documentation, built and run as the docs_snippets test suite.

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <memory>

TEST(Craters, DefaultLayer) {
    // --8<-- [start:default_layer]
    terraingen::TerrainGenerator terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(257, 257, 1.0);
    terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>());
    const auto heights = terraingen::Evaluate(terrain);
    // --8<-- [end:default_layer]
    EXPECT_EQ(heights.Width(), 257);
}

TEST(Craters, PublishedModels) {
    // --8<-- [start:published_models]
    const terraingen::EquilibriumSurface surface{
        std::make_shared<terraingen::SplicedSfd>(terraingen::ExtendedNeukumProductionFunction()),
        std::make_shared<terraingen::PowerLawSfd>(terraingen::TraskEquilibrium()), 3.5,
        terraingen::NeukumLunarChronology};
    const auto ages = surface.Ages();
    const terraingen::CraterModels models{surface.Density(), ages,
                                          std::make_shared<terraingen::FreshnessDegradation>(
                                              ages, terraingen::LunarFreshCraterMorphometry),
                                          std::make_shared<terraingen::FassettThomsonProfile>()};
    terraingen::CraterLayerOptions options;
    options.seed = 7;
    options.generation.dMin = 4.0;
    options.generation.dMax = 120.0;
    auto craters = std::make_shared<terraingen::CraterLayer>(models, options);
    // --8<-- [end:published_models]

    terraingen::TerrainGenerator terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(257, 257, 1.0);
    terrain.layers.push_back(craters);
    const auto heights = terraingen::Evaluate(terrain);

    // --8<-- [start:crater_list]
    const auto placed =
        terraingen::PlacedCraters(terrain, terraingen::RasterGrid::FromRaster(heights));
    const auto& diameters = placed.craters.Diameters();
    // --8<-- [end:crater_list]
    EXPECT_GT(diameters.size(), 0u);
}
