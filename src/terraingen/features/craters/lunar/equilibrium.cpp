#include "terraingen/features/craters/lunar/equilibrium.h"

#include <cmath>

namespace terraingen {

PowerLaw TraskEquilibrium() {
    return PowerLaw(std::pow(10.0, -1.1), 2.0);
}

PowerLaw HartmannEquilibrium() {
    return PowerLaw(PowerLawCoefficientFromKilometres(std::pow(10.0, -1.14), 1.83), 1.83);
}

}  // namespace terraingen
