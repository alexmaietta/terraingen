#include "terraingen/layer/craters/degradation/none.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

NoDegradation::NoDegradation(double depthRatio) : depthRatio_(depthRatio) {
    if (!(std::isfinite(depthRatio_) && depthRatio_ > 0.0)) {
        throw std::invalid_argument("Depth/diameter ratio must be positive and finite");
    }
}

CraterState NoDegradation::State(const Crater& crater) const {
    const double depth = FreshCraterDepthRatio(crater.diameter);
    const CraterMorphometry fresh{depthRatio_ > 0.0 ? depthRatio_ : depth, kFreshRimHeightRatio};
    return {fresh, fresh};
}

}  // namespace terraingen
