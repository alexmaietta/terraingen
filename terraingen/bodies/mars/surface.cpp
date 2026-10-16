/// @file
/// @brief Martian surfaces of a given age.
#include "terraingen/bodies/mars/surface.h"

#include "terraingen/distribution/ivanov.h"
#include "terraingen/distribution/production_surface.h"
#include "terraingen/layers/craters/ages/uniform.h"

#include <memory>
#include <stdexcept>
#include <utility>

namespace terraingen {

SurfaceModel MarsSurface(double ageGyr) {
    if (!(ageGyr > 0.0)) {
        throw std::domain_error("A Martian surface must be older than 0 Gyr");
    }
    auto production = std::make_shared<LogPolynomialSfd>(IvanovMarsProductionFunction());
    return {
        std::make_shared<ProductionSurface>(std::move(production), ageGyr, IvanovMarsChronology),
        std::make_shared<UniformAges>(ageGyr)};
}

}  // namespace terraingen
