/// @file
/// @brief Terrains that the benchmarks evaluate.
#pragma once

#include "support/lunar_craters.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen::benchmarks {

/// A 3.5 Gyr lunar mare: craters from 2.5 m (the lunar model's lower limit) to `maxDiameter`, the
/// Fassett and Thomson profile degraded by age, on rolling noise, over a flat `size` x `size` m
/// source of `pixelSize` cells centered on the origin. `cacheCraters` sizes the crater cell cache
/// (0 for none). On a sphere `body` the source is centered on longitude and latitude 0.
inline TerrainGenerator LunarMare(double size, double pixelSize, double maxDiameter = 200.0,
                                  std::size_t cacheCraters = 4000000,
                                  CraterDatum datum = CraterDatum::None, const Surface& body = {}) {
    TerrainGenerator terrain;
    terrain.surface = body;
    const auto cells = static_cast<std::size_t>(size / pixelSize);
    terrain.source = std::make_shared<FlatSource>(cells, cells, pixelSize / body.UnitLength());
    SyntheticTerrainParams noise;
    noise.layers[0].fractal.wavelength = 400.0f;
    noise.layers[0].fractal.amplitude = 8.0f;
    terrain.layers.push_back(std::make_shared<NoiseRelief>(noise, Vec2d{}, body));
    const auto density = testing::LunarCraterDensity(3.5);
    const auto ages = testing::LunarCraterAges(3.5);
    CraterLayerOptions options;
    options.generation.dMin = 2.5;
    options.generation.dMax = maxDiameter;
    options.cacheCraters = cacheCraters;
    options.apply.datum = datum;
    options.surface = body;
    terrain.layers.push_back(std::make_shared<CraterLayer>(
        CraterModels{density, ages,
                     std::make_shared<FreshnessDegradation>(ages, LunarFreshCraterMorphometry),
                     std::make_shared<FassettThomsonProfile>()},
        options));
    return terrain;
}

}  // namespace terraingen::benchmarks
