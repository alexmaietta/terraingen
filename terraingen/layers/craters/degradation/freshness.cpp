/// @file
/// @brief `FreshnessDegradation`: one erosion factor from the share of lifetime left.
#include "terraingen/layers/craters/degradation/freshness.h"

#include <stdexcept>
#include <utility>

namespace terraingen {

FreshnessDegradation::FreshnessDegradation(std::shared_ptr<const CraterAgeModel> ages,
                                           FreshMorphometry fresh, double cutoffDepthRatio)
    : ages_(std::move(ages)), fresh_(std::move(fresh)), cutoffDepthRatio_(cutoffDepthRatio) {
    if (!ages_) {
        throw std::invalid_argument("Freshness degradation needs an age model");
    }
    if (!fresh_) {
        throw std::invalid_argument("Freshness degradation needs the fresh crater morphometry");
    }
    if (!(cutoffDepthRatio_ > 0.0 && cutoffDepthRatio_ < 0.1)) {
        throw std::invalid_argument("Cutoff depth/diameter ratio must be in (0, 0.1)");
    }
}

CraterState FreshnessDegradation::State(const Crater& crater) const {
    const CraterMorphometry fresh = fresh_(crater.diameter);
    const double floor = cutoffDepthRatio_ / fresh.depthRatio;
    const double erosion =
        floor + (1.0 - floor) * ages_->RemainingLifetime(crater.diameter, crater.ageGyr);
    return {fresh, {erosion * fresh.depthRatio, erosion * fresh.rimHeightRatio}};
}

}  // namespace terraingen
