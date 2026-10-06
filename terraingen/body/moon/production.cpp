#include "terraingen/body/moon/production.h"

#include "terraingen/distribution/power_law.h"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

namespace terraingen {

ProductionSurface::ProductionSurface(std::shared_ptr<const SizeFrequencyDistribution> production,
                                     double ageGyr)
    : SizeFrequencyDistribution(SfdKind::Density,
                                production ? production->ValidRange() : DiameterRange{}),
      production_(std::move(production)), ageGyr_(ageGyr) {
    if (!production_ || production_->Kind() != SfdKind::ProductionRate) {
        throw std::invalid_argument("A production surface needs a production rate distribution");
    }
    SetAllowExtrapolation(production_->AllowsExtrapolation());
    scale_ = NeukumLunarChronology(ageGyr_) / production_->Cumulative(1000.0);
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

LogPolynomialSfd NeukumProductionFunction() {
    return LogPolynomialSfd({-3.076756, -3.557528, 0.781027, 1.021521, -0.156012, -0.444058,
                             0.019977, 0.086850, -0.005874, -0.006809, 8.25e-4, 5.54e-5},
                            DiameterRange{10.0, 300000.0}, SfdKind::ProductionRate, 1000.0, 1e6);
}

SplicedSfd ExtendedNeukumProductionFunction() {
    constexpr double kJoin = 10.0;
    constexpr double kSlope = 2.85;
    auto npf = std::make_shared<LogPolynomialSfd>(NeukumProductionFunction());
    const double coefficient = npf->Cumulative(kJoin) * std::pow(kJoin, kSlope);
    auto smallCraters = std::make_shared<PowerLawSfd>(coefficient, kSlope, SfdKind::ProductionRate);
    return SplicedSfd({{2.5, std::move(smallCraters)}, {kJoin, std::move(npf)}},
                      DiameterRange{2.5, 300000.0});
}

}  // namespace terraingen
