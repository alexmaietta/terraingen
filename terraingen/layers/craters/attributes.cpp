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
    const double fresh = state.fresh.depthRatio;
    return {-profile.Height(0.0, diameter, state), rim, state.current.depthRatio,
            fresh > 0.0 ? state.current.depthRatio / fresh : 1.0};
}

}  // namespace terraingen
