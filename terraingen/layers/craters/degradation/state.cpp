#include "terraingen/layers/craters/degradation/state.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

double FreshCraterDepthRatio(double diameter) {
    if (!(std::isfinite(diameter) && diameter > 0.0)) {
        throw std::domain_error("Diameter must be positive and finite");
    }
    if (diameter <= 10.0) {
        return 0.10;
    }
    if (diameter < 40.0) {
        return 0.11;
    }
    if (diameter < 100.0) {
        return 0.13;
    }
    if (diameter < 200.0) {
        return 0.15;
    }
    if (diameter < 400.0) {
        return 0.17;
    }
    return 0.21;
}

}  // namespace terraingen
