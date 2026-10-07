#include "terraingen/layers/craters/attributes.h"

#include <cmath>

namespace terraingen {

CraterAttributes ComputeCraterAttributes(const Crater& crater, const CraterState& state,
                                         const CraterProfile& profile) {
    const double diameter = crater.diameter;
    double rim = -INFINITY;
    for (int step = 70; step <= 130; ++step) {
        const double radii = 0.01 * static_cast<double>(step);
        rim = std::fmax(rim, profile.Height(0.5 * radii * diameter, diameter, state));
    }
    double floor = INFINITY;
    for (int step = 0; step <= 100; ++step) {
        floor = std::fmin(floor, profile.Height(0.005 * step * diameter, diameter, state));
    }
    const double fresh = state.fresh.depthRatio;
    return {-floor, rim, state.current.depthRatio,
            fresh > 0.0 ? state.current.depthRatio / fresh : 1.0, profile.IsComplex(diameter)};
}

}  // namespace terraingen
