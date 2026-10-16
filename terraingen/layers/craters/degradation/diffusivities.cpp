#include "terraingen/layers/craters/degradation/diffusivities.h"

#include <cmath>

namespace terraingen {

PowerLawDiffusivity FassettThomson2014Diffusivity() {
    return {5.5, 0.0, 0.0};
}

PowerLawDiffusivity Fassett2022HartmannDiffusivity() {
    return {3.6, 0.87, 5.0};
}

PowerLawDiffusivity Fassett2022TraskDiffusivity() {
    return {3.1, 1.05, 5.0};
}

double FassettThomson2014DiffusionAge(double ageGyr) {
    const double t = ageGyr;
    const double kappaT =
        (((435.83 * t - 3621.2) * t + 11204.0) * t - 16811.0) * t * t + 17546.0 * t;
    return std::fmax(0.0, kappaT);
}

}  // namespace terraingen
