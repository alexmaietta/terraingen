#include "terraingen/features/craters/profiles/martin_parkes_dunstan.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

double MartinParkesDunstanProfile::Height(double distance, double diameter,
                                          const CraterState& state) const {
    const double depth = state.current.depthRatio * diameter;
    const double rim = state.current.rimHeightRatio * diameter;
    const double freshRim = state.fresh.rimHeightRatio * diameter;
    if (!(depth > 0.0 && rim >= 0.0 && rim <= freshRim)) {
        throw std::invalid_argument(
            "Crater state needs a positive depth and 0 <= rim height <= fresh rim height");
    }
    const double radius = 0.5 * diameter;
    const double x = distance / radius - 1.0;
    if (x >= 1.0) {
        return 0.0;
    }
    const double k = freshRim - rim + depth;
    const double alpha = (rim - freshRim) / k;
    if (x <= alpha) {
        return (k * x + 2.0 * k) * x + freshRim;
    }
    if (x <= 0.0) {
        return k * (alpha + 1.0) / alpha * x * x + rim;
    }
    const double ejectaScale = 0.14 * std::pow(radius, 0.74);
    const double offset = rim - ejectaScale;
    const double gradient = -ejectaScale / 8.0 - offset;
    const auto ejecta = [&](double at) {
        return ejectaScale / ((at + 1.0) * (at + 1.0) * (at + 1.0)) + offset + gradient * at;
    };
    const double beta = std::fmin(1.0, 1.5 * (freshRim - rim) / k);
    if (x <= beta) {
        const double a = (rim - ejecta(beta)) / (2.0 * beta * beta * beta);
        return (a * x - 3.0 * a * beta) * x * x + rim;
    }
    return ejecta(x);
}

}  // namespace terraingen
