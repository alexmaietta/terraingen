// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Small terrains built from objects, shared by the evaluation tests.
#pragma once

#include "terraingen/pipeline/terrain.h"
#include "terraingen/terraingen.h"

#include <memory>
#include <vector>

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

/// The 96 x 64 flat source of 2 m cells at 5 m of `Rolling`, one for every call.
inline std::shared_ptr<const Source> RollingSource() {
    static const std::shared_ptr<const Source> source =
        std::make_shared<FlatSource>(96, 64, 2.0, 5.0f);
    return source;
}

/// The 6 to 50 m craters of `Rolling`, shaped with `apply`.
inline LayerPtr RollingCraters(const CraterApplyOptions& apply = {}) {
    CraterLayerOptions options;
    options.generation.minDiameter = 6.0;
    options.generation.maxDiameter = 50.0;
    options.apply = apply;
    options.sourceResolvesPixels = std::nullopt;
    return std::make_shared<CraterLayer>(PowerLawCraterModels(), options,
                                         TerrainBase{Surface{}, RollingSource()});
}

/// `RollingSource`, rolling noise, then 6 to 50 m craters shaped with `apply`.
inline Terrain Rolling(const CraterApplyOptions& apply = {}) {
    std::vector<Fractal> noise{Fractal{}};
    noise[0].wavelength = 80.0f;
    noise[0].amplitude = 6.0f;
    return Terrain(Surface{}, RollingSource(),
                   {std::make_shared<NoiseLayer>(noise), RollingCraters(apply)});
}

/// `Rolling` with rounded crater rims.
inline Terrain Smoothed() {
    CraterApplyOptions apply;
    apply.rimSmoothing.sigmaRadii = 0.05;
    return Rolling(apply);
}

/// @brief `Rolling` with its craters cut to a datum fitted to the ground.
/// @param datum The datum.
/// @return The terrain.
inline Terrain Datum(CraterDatum datum = CraterDatum::Plane) {
    Terrain terrain = Rolling();
    CraterApplyOptions apply;
    apply.datum = datum;
    terrain.SetLayer(terrain.Layers().size() - 1, RollingCraters(apply));
    return terrain;
}

}  // namespace terraingen::testing
