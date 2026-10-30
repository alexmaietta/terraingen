// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Randomize a Terrain Each Episode
///
/// `Reseed` gives a copy of a terrain whose seeded layers and synthetic source each draw from a
/// seed derived from the one given, for domain randomization in reinforcement learning. Nothing is
/// read from disk again, and measured data such as a DEM stays as it is. The same seed always
/// gives the same terrain, so an episode can be replayed from its seed alone.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, RandomizeATerrainEachEpisode) {
    // --8<-- [start:randomize_a_terrain_each_episode]
    const Terrain terrain(Surface{}, std::make_shared<FlatSource>(129, 129, 1.0),
                          {std::make_shared<NoiseLayer>(Fractal::Fbm(1, 120.0, 4.0)),
                           std::make_shared<CraterLayer>()});
    std::vector<Raster<float>> episodes;
    for (std::uint64_t episode = 0; episode < 3; ++episode) {
        episodes.push_back(terrain.Reseed(episode).Evaluate());  // new hills and craters
    }
    // --8<-- [end:randomize_a_terrain_each_episode]
    const auto same = [](const Raster<float>& a, const Raster<float>& b) {
        return std::equal(a.Values().begin(), a.Values().end(), b.Values().begin());
    };
    EXPECT_FALSE(same(episodes[0], episodes[1]));
    EXPECT_TRUE(same(episodes[2], terrain.Reseed(2).Evaluate()));
}

}  // namespace
}  // namespace terraingen
