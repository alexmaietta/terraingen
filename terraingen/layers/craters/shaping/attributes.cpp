#include "terraingen/layers/craters/shaping/attributes.h"

#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/craters/shaping/resolved_state.h"

#include <cmath>
#include <limits>

namespace terraingen {

CraterAttributes ComputeCraterAttributes(const Crater& crater, const CraterState& state,
                                         const CraterProfile& profile) {
    const double diameter = crater.diameter;
    double rim = -std::numeric_limits<double>::infinity();
    for (int step = 70; step <= 130; ++step) {
        const double radii = 0.01 * static_cast<double>(step);
        rim = std::fmax(rim, profile.Height(0.5 * radii * diameter, diameter, state));
    }
    double floor = std::numeric_limits<double>::infinity();
    for (int step = 0; step <= 100; ++step) {
        floor = std::fmin(floor, profile.Height(0.005 * step * diameter, diameter, state));
    }
    return {-floor, rim, state.current.depthRatio, state.Freshness(), profile.IsComplex(diameter)};
}

CraterAttributes CraterShapeModels::Attributes(const Crater& crater) const {
    return ComputeCraterAttributes(crater, ResolvedCraterState(*this, crater), *profile);
}

}  // namespace terraingen
