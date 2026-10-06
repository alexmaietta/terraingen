#pragma once

#include "terraingen/distributions/size_frequency.h"
#include "terraingen/features/craters/ages/age_model.h"

#include <memory>

namespace terraingen {

/// A surface's crater density and age model, the inputs to crater generation.
struct SurfaceModel {
    std::shared_ptr<const SizeFrequencyDistribution> density;
    std::shared_ptr<const CraterAgeModel> ages;
};

/// The DSNE construction of a lunar surface `ageGyr` old: density
/// min(`ExtendedNeukumProductionFunction` surface, `TraskEquilibrium`), valid from 2.5 m to 300 km,
/// with `SteadyStateAges` from the same two distributions.
///
/// Reference: NASA SLS-SPEC-159 (DSNE) Revision I, Section 3.4.1.1.
/// @throws std::domain_error if `ageGyr` is outside [0, `kMaxLunarAgeGyr`].
SurfaceModel LunarSurface(double ageGyr);

}  // namespace terraingen
