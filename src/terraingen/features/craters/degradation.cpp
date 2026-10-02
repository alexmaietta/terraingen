#include "terraingen/features/craters/degradation.h"

#include <cmath>
#include <stdexcept>
#include <utility>

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

FreshnessDegradation::FreshnessDegradation(std::shared_ptr<const CraterAgeModel> ages,
                                           double cutoffDepthRatio)
    : ages_(std::move(ages)), cutoffDepthRatio_(cutoffDepthRatio) {
    if (!ages_) {
        throw std::invalid_argument("Freshness degradation needs an age model");
    }
    if (!(cutoffDepthRatio_ > 0.0 && cutoffDepthRatio_ < 0.1)) {
        throw std::invalid_argument("Cutoff depth/diameter ratio must be in (0, 0.1)");
    }
}

CraterState FreshnessDegradation::State(const Crater& crater) const {
    const CraterMorphometry fresh{FreshCraterDepthRatio(crater.diameter), kFreshRimHeightRatio};
    const double floor = cutoffDepthRatio_ / fresh.depthRatio;
    const double erosion = floor + (1.0 - floor) * ages_->Freshness(crater.diameter, crater.ageGyr);
    return {fresh, {erosion * fresh.depthRatio, erosion * fresh.rimHeightRatio}};
}

}  // namespace terraingen
