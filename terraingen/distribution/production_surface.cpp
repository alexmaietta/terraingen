#include "terraingen/distribution/production_surface.h"

#include <stdexcept>
#include <utility>

namespace terraingen {

ProductionSurface::ProductionSurface(std::shared_ptr<const SizeFrequencyDistribution> production,
                                     double ageGyr, const Chronology& chronology)
    : SizeFrequencyDistribution(SfdKind::Density,
                                production ? production->ValidRange() : DiameterRange{}),
      production_(std::move(production)), ageGyr_(ageGyr) {
    if (!production_ || production_->Kind() != SfdKind::ProductionRate) {
        throw std::invalid_argument("A production surface needs a production rate distribution");
    }
    if (!chronology) {
        throw std::invalid_argument("A production surface needs a chronology");
    }
    SetAllowExtrapolation(production_->AllowsExtrapolation());
    scale_ = chronology(ageGyr_) / production_->Cumulative(1000.0);
}

double ProductionSurface::CumulativeAt(double diameter) const {
    return scale_ * CumulativeOf(*production_, diameter);
}

double ProductionSurface::IncrementalAt(double diameter) const {
    return scale_ * IncrementalOf(*production_, diameter);
}

double ProductionSurface::InvertCumulative(double target, double dMin, double dMax) const {
    return InvertCumulativeOf(*production_, target / scale_, dMin, dMax);
}

}  // namespace terraingen
