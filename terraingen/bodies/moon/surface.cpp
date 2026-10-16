#include "terraingen/bodies/moon/surface.h"

#include "terraingen/bodies/moon/equilibrium.h"
#include "terraingen/bodies/moon/production.h"
#include "terraingen/bodies/moon/steady_state.h"
#include "terraingen/distribution/minimum.h"

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
        std::make_shared<SteadyStateAges>(production, equilibrium, ageGyr)};
}

}  // namespace terraingen
