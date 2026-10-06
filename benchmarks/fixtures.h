#pragma once

#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen::benchmarks {

/// A 3.5 Gyr lunar mare: craters from 2.5 m (the lunar model's lower limit) to `maxDiameter`, the
/// Fassett and Thomson profile degraded by age, on rolling noise, over a flat `size` x `size` m
/// source of `pixelSize` cells centred on the origin. `cacheCraters` sizes the crater cell cache (0
/// for none).
inline Terrain LunarMare(double size, double pixelSize, double maxDiameter = 200.0,
                         std::size_t cacheCraters = 4000000,
                         CraterDatum datum = CraterDatum::None) {
    Terrain terrain;
    const auto cells = static_cast<std::size_t>(size / pixelSize);
    terrain.source = std::make_shared<FlatSource>(cells, cells, pixelSize);
    SyntheticTerrainParams noise;
    noise.layers[0].fractal.wavelength = 400.0f;
    noise.layers[0].fractal.amplitude = 8.0f;
    terrain.layers.push_back(std::make_shared<NoiseRelief>(noise));
    const SurfaceModel surface = LunarSurface(3.5);
    CraterLayerOptions options;
    options.generation.dMin = 2.5;
    options.generation.dMax = maxDiameter;
    options.cacheCraters = cacheCraters;
    options.apply.datum = datum;
    terrain.layers.push_back(std::make_shared<CraterLayer>(
        CraterModels{surface.density, surface.ages,
                     std::make_shared<FreshnessDegradation>(surface.ages),
                     std::make_shared<FassettThomsonProfile>()},
        options));
    return terrain;
}

}  // namespace terraingen::benchmarks
