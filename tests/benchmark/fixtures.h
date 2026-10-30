// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Terrains that the benchmarks evaluate.
#pragma once

#include "support/lunar_craters.h"
#include "terraingen/terraingen.h"

#include <memory>
#include <vector>

namespace terraingen::benchmarks {

/// A 3.5 Gyr lunar mare: craters from 2.5 m (the lunar model's lower limit) to `maxDiameter`, the
/// Fassett and Thomson profile degraded by age, on rolling noise, over a flat `size` x `size` m
/// source of `pixelSize` cells centered on the origin. `cacheCraters` sizes the crater cell cache
/// (0 for none). On a sphere `body` the source is centered on longitude and latitude 0.
inline Terrain LunarMare(double size, double pixelSize, double maxDiameter = 200.0,
                         std::size_t cacheCraters = 4000000, CraterDatum datum = CraterDatum::None,
                         const Surface& body = {}) {
    const auto cells = static_cast<std::size_t>(size / pixelSize);
    std::vector<terraingen::Fractal> noise{terraingen::Fractal{}};
    noise[0].wavelength = 400.0f;
    noise[0].amplitude = 8.0f;
    const auto density = testing::LunarCraterDensity(3.5);
    const auto ages = testing::LunarCraterAges(3.5);
    CraterLayerOptions options;
    options.generation.dMin = 2.5;
    options.generation.dMax = maxDiameter;
    options.cacheCraters = cacheCraters;
    options.apply.datum = datum;
    return Terrain(
        body, std::make_shared<FlatSource>(cells, cells, pixelSize / body.UnitLength()),
        {std::make_shared<NoiseLayer>(noise),
         std::make_shared<CraterLayer>(
             CraterModels{density, ages,
                          std::make_shared<FreshnessDegradation>(ages, LunarFreshCraterMorphometry),
                          std::make_shared<FassettThomsonProfile>()},
             options, body)});
}

/// The grid of `terrain`'s source.
inline RasterGrid SourceGrid(const Terrain& terrain) {
    return terrain.Source()->Grid();
}

}  // namespace terraingen::benchmarks
