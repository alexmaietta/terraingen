/// @file
/// @brief A body's surface as crater generation needs it: crater density and crater ages.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/ages/age_model.h"

#include <memory>

namespace terraingen {

/// @brief A surface's crater density and age model, the inputs to crater generation.
struct SurfaceModel {
    std::shared_ptr<const SizeFrequencyDistribution> density;  ///< Craters per m^2.
    std::shared_ptr<const CraterAgeModel> ages;                ///< Crater lifetimes and ages.
};

}  // namespace terraingen
