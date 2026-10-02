#include "terraingen/features/craters/lunar_cratering.h"

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

PowerLaw TraskEquilibrium() {
    return PowerLaw(std::pow(10.0, -1.1), 2.0);
}

PowerLaw HartmannEquilibrium() {
    return PowerLaw(PowerLawCoefficientFromKilometres(std::pow(10.0, -1.14), 1.83), 1.83);
}

PiecewisePowerLaw MooreDistribution(MooreTerrain terrain) {
    constexpr double kRoundingTolerance = 5e-3;
    switch (terrain) {
    case MooreTerrain::RoughMare:
        return PiecewisePowerLaw({{0.0, 0.1, 2.0}, {100.0, 10.0, 3.0}}, SfdKind::Density,
                                 DiameterRange{}, false, kRoundingTolerance);
    case MooreTerrain::SmoothMare:
        return PiecewisePowerLaw({{0.0, 0.1, 2.0},
                                  {40.0, std::pow(10.0, 0.602), 3.0},
                                  {100.0, std::pow(10.0, -2.038), 1.68},
                                  {200.0, 10.0, 3.0}},
                                 SfdKind::Density, DiameterRange{}, false, kRoundingTolerance);
    case MooreTerrain::Uplands:
        return PiecewisePowerLaw({{0.0, 0.1, 2.0}, {1000.0, 100.0, 3.0}}, SfdKind::Density,
                                 DiameterRange{}, false, kRoundingTolerance);
    default:
        return PiecewisePowerLaw({{0.0, 0.1, 2.0}}, SfdKind::Density, DiameterRange{0.0, 10000.0});
    }
}

PiecewisePowerLaw ViperSpecification() {
    return PiecewisePowerLaw({{0.0, 29174.0 / 1e6, 1.92}, {80.0, 156228.0 / 1e6, 2.389}},
                             SfdKind::Density, DiameterRange{}, true);
}

double NeukumLunarChronology(double ageGyr) {
    if (!(ageGyr >= 0.0 && ageGyr <= kMaxLunarAgeGyr)) {
        throw std::domain_error("Lunar surface age must be between 0 and 4.6 Gyr");
    }
    return (5.44e-14 * std::expm1(6.93 * ageGyr) + 8.38e-4 * ageGyr) / 1e6;
}

double NeukumLunarChronologyRate(double ageGyr) {
    if (!(ageGyr >= 0.0 && ageGyr <= kMaxLunarAgeGyr)) {
        throw std::domain_error("Lunar surface age must be between 0 and 4.6 Gyr");
    }
    return (5.44e-14 * 6.93 * std::exp(6.93 * ageGyr) + 8.38e-4) / 1e6;
}

LogPolynomial NeukumProductionFunction() {
    return LogPolynomial({-3.076756, -3.557528, 0.781027, 1.021521, -0.156012, -0.444058, 0.019977,
                          0.086850, -0.005874, -0.006809, 8.25e-4, 5.54e-5},
                         DiameterRange{10.0, 300000.0}, SfdKind::ProductionRate, 1000.0, 1e6);
}

SplicedSfd ExtendedNeukumProductionFunction() {
    constexpr double kJoin = 10.0;
    constexpr double kSlope = 2.85;
    auto npf = std::make_shared<LogPolynomial>(NeukumProductionFunction());
    const double coefficient = npf->Cumulative(kJoin) * std::pow(kJoin, kSlope);
    auto smallCraters = std::make_shared<PowerLaw>(coefficient, kSlope, SfdKind::ProductionRate);
    return SplicedSfd({{2.5, std::move(smallCraters)}, {kJoin, std::move(npf)}},
                      DiameterRange{2.5, 300000.0});
}

}  // namespace terraingen
