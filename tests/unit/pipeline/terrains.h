/// @file
/// @brief Small terrain generators built from objects, shared by the evaluation tests.
#pragma once

#include "terraingen/pipeline/generator.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen::testing {

/// @brief Fresh power-law crater models: N(D) = 0.05 D^-2, 1 Gyr uniform ages, Fassett-Thomson
/// bowls.
/// @return The models.
inline CraterModels PowerLawCraterModels() {
    return CraterModels{std::make_shared<PowerLawSfd>(0.05, 2.0),
                        std::make_shared<UniformAges>(1.0),
                        std::make_shared<NoDegradation>(LunarFreshCraterMorphometry),
                        std::make_shared<FassettThomsonProfile>()};
}

/// A 96 x 64 flat source of 2 m cells at 5 m, rolling noise, then 6 to 50 m craters shaped with
/// `apply`.
inline TerrainGenerator Rolling(const CraterApplyOptions& apply = {}) {
    TerrainGenerator terrain;
    terrain.source = std::make_shared<FlatSource>(96, 64, 2.0, 5.0f);
    SyntheticTerrainParams noise{{terraingen::NoiseLayer{}}};
    noise.layers[0].fractal.wavelength = 80.0f;
    noise.layers[0].fractal.amplitude = 6.0f;
    terrain.layers.push_back(std::make_shared<NoiseRelief>(noise));
    CraterLayerOptions options;
    options.generation.dMin = 6.0;
    options.generation.dMax = 50.0;
    options.apply = apply;
    terrain.layers.push_back(std::make_shared<CraterLayer>(PowerLawCraterModels(), options));
    return terrain;
}

/// `Rolling` with rounded crater rims.
inline TerrainGenerator Smoothed() {
    CraterApplyOptions apply;
    apply.rimSmoothing.sigmaRadii = 0.05;
    return Rolling(apply);
}

/// @brief `Rolling` with its craters cut to a datum fitted to the ground.
/// @param datum The datum.
/// @return The terrain.
inline TerrainGenerator Datum(CraterDatum datum = CraterDatum::Plane) {
    TerrainGenerator terrain = Rolling();
    CraterLayerOptions options;
    options.generation.dMin = 6.0;
    options.generation.dMax = 50.0;
    options.apply.datum = datum;
    terrain.layers.back() = std::make_shared<CraterLayer>(PowerLawCraterModels(), options);
    return terrain;
}

}  // namespace terraingen::testing
