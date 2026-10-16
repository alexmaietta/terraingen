/// @file
/// @brief The DSNE model of a lunar surface of a given age.
#include "terraingen/bodies/moon/surface.h"

#include "terraingen/distribution/equilibrium.h"
#include "terraingen/distribution/minimum.h"
#include "terraingen/distribution/neukum.h"
#include "terraingen/distribution/production_surface.h"
#include "terraingen/layers/craters/ages/steady_state.h"

#include <memory>
#include <vector>

namespace terraingen {

SurfaceModel LunarSurface(double ageGyr) {
    auto production = std::make_shared<SplicedSfd>(ExtendedNeukumProductionFunction());
    auto equilibrium = std::make_shared<PowerLawSfd>(TraskEquilibrium());
    auto surface = std::make_shared<ProductionSurface>(production, ageGyr, NeukumLunarChronology);
    return SurfaceModel{
        std::make_shared<MinimumSfd>(
            std::vector<std::shared_ptr<const SizeFrequencyDistribution>>{surface, equilibrium}),
        std::make_shared<SteadyStateAges>(production, equilibrium, ageGyr, NeukumLunarChronology)};
}

}  // namespace terraingen
