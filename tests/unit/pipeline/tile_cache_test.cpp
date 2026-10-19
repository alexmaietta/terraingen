#include "terraingen/pipeline/tiles.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

using terraingen::TileCache;
using terraingen::TileKey;
using terraingen::TileScheme;

namespace {

terraingen::Terrain Noise() {
    terraingen::Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(64, 64, 1.0);
    terraingen::SyntheticTerrainParams noise;
    noise.layers[0].fractal.wavelength = 30.0f;
    terrain.layers.push_back(std::make_shared<terraingen::NoiseRelief>(noise));
    return terrain;
}

}  // namespace

TEST(TileCache, EvaluatesEachTileOnceAndStaysWithinItsBudget) {
    const TileScheme scheme{{0.0, 0.0}, 64.0, 16};
    TileCache cache(Noise(), scheme, 16 * 16 * 4 * 32);
    const auto tile = cache.Get({2, 1, 3});
    EXPECT_EQ(cache.Get({2, 1, 3}), tile);
    const auto expected = terraingen::EvaluateTile(Noise(), scheme, {2, 1, 3});
    EXPECT_TRUE(
        std::equal(tile->Values().begin(), tile->Values().end(), expected.Values().begin()));
    for (std::int64_t x = 0; x < 40; ++x) {
        cache.Get({3, x, 0});
    }
    EXPECT_LE(cache.Size(), cache.MaxBytes());
    EXPECT_GT(cache.Evictions(), 0u);
    EXPECT_THROW(TileCache(Noise(), TileScheme{{0.0, 0.0}, 0.0, 16}), std::invalid_argument);
    EXPECT_THROW(TileCache(terraingen::Terrain{nullptr, {nullptr}}, scheme), std::invalid_argument);
    EXPECT_THROW(TileCache(Noise(), scheme, 0), std::invalid_argument);
}
