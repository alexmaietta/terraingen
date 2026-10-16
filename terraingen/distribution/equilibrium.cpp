/// @file
/// @brief The Trask (1966) and Hartmann (1984) lunar crater equilibrium densities.
#include "terraingen/distribution/equilibrium.h"

#include <cmath>

namespace terraingen {

PowerLawSfd TraskEquilibrium() {
    return PowerLawSfd(std::pow(10.0, -1.1), 2.0);
}

PowerLawSfd HartmannEquilibrium() {
    return PowerLawSfd(PowerLawCoefficientFromKilometres(std::pow(10.0, -1.14), 1.83), 1.83);
}

}  // namespace terraingen
