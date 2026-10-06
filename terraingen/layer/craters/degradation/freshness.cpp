#include "terraingen/layer/craters/degradation/freshness.h"

#include <stdexcept>
#include <utility>

namespace terraingen {

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
