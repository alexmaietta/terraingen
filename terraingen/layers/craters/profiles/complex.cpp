#include "terraingen/layers/craters/profiles/complex.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace terraingen {

double KilometrePowerLaw::At(double diameter, bool volume) const {
    const double kilometres = coefficient * std::pow(diameter / 1000.0, exponent);
    return volume ? kilometres * 1e9 : kilometres * 1000.0;
}

void ValidateComplexCraterShape(const ComplexCraterShape& shape) {
    if (!(std::isfinite(shape.transitionDiameter) && shape.transitionDiameter > 0.0)) {
        throw std::invalid_argument("The complex crater transition diameter must be positive");
    }
    for (const KilometrePowerLaw& law : {shape.floorDiameter, shape.peakHeight, shape.peakVolume}) {
        if (!(std::isfinite(law.coefficient) && law.coefficient > 0.0 &&
              std::isfinite(law.exponent))) {
            throw std::invalid_argument("Complex crater power laws need a positive coefficient");
        }
    }
    if (!(std::isfinite(shape.peakFitMaxDiameter) && shape.peakFitMaxDiameter >= 0.0)) {
        throw std::invalid_argument("The peak fit limit must not be negative");
    }
    if (!(std::isfinite(shape.ejectaExponent) && shape.ejectaExponent > 0.0 &&
          std::isfinite(shape.reachRadii) && shape.reachRadii > 1.0)) {
        throw std::invalid_argument(
            "Complex craters need a positive ejecta exponent and a reach over 1 radius");
    }
}

ComplexCraterProfile::ComplexCraterProfile(std::shared_ptr<const CraterProfile> simple,
                                           ComplexCraterShape shape)
    : simple_(std::move(simple)), shape_(shape) {
    if (!simple_) {
        throw std::invalid_argument("A complex crater profile needs a simple one");
    }
    ValidateComplexCraterShape(shape_);
}

double ComplexCraterProfile::ReachRadii() const {
    return std::fmax(simple_->ReachRadii(), shape_.reachRadii);
}

double ComplexCraterProfile::Height(double distance, double diameter,
                                    const CraterState& state) const {
    if (!IsComplex(diameter)) {
        return simple_->Height(distance, diameter, state);
    }
    const double radius = 0.5 * diameter;
    const double x = distance / radius;
    const double depth = state.current.depthRatio * diameter;
    const double rim = state.current.rimHeightRatio * diameter;
    if (x >= 1.0) {
        const double edge = std::pow(shape_.reachRadii, -shape_.ejectaExponent);
        if (x >= shape_.reachRadii) {
            return 0.0;
        }
        return rim * (std::pow(x, -shape_.ejectaExponent) - edge) / (1.0 - edge);
    }
    const double floor = std::min(0.9, shape_.floorDiameter.At(diameter) / diameter);
    double height = rim - depth;
    if (x > floor) {
        const double t = (x - floor) / (1.0 - floor);
        height += depth * t * t;
    }
    const double fitted =
        shape_.peakFitMaxDiameter > 0.0 ? std::fmin(diameter, shape_.peakFitMaxDiameter) : diameter;
    const double peakHeight = shape_.peakHeight.At(fitted);
    const double base =
        std::sqrt(3.0 * shape_.peakVolume.At(fitted, true) / (std::numbers::pi * peakHeight)) *
        (diameter / fitted);
    if (distance < base) {
        const double fresh = state.fresh.depthRatio;
        const double kept = fresh > 0.0 ? state.current.depthRatio / fresh : 1.0;
        const double u = 1.0 - (distance / base) * (distance / base);
        height += kept * peakHeight * u * u;
    }
    return height;
}

}  // namespace terraingen
