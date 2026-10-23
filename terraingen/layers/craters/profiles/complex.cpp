// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/profiles/complex.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

double KilometerPowerLaw::At(double diameter, bool volume) const {
    return static_cast<double>(KilometerLaw(static_cast<float>(coefficient),
                                            static_cast<float>(exponent),
                                            static_cast<float>(diameter), volume));
}

void ValidateComplexCraterShape(const ComplexCraterShape& shape) {
    if (!IsPositive(shape.transitionDiameter)) {
        throw std::invalid_argument("The complex crater transition diameter must be positive");
    }
    const KilometerPowerLaw peakWidth = shape.peakDiameter ? *shape.peakDiameter : shape.peakVolume;
    for (const KilometerPowerLaw& law : {shape.floorDiameter, shape.peakHeight, peakWidth}) {
        if (!(IsPositive(law.coefficient) && std::isfinite(law.exponent))) {
            throw std::invalid_argument("Complex crater power laws need a positive coefficient");
        }
    }
    if (!IsNonNegative(shape.peakFitMaxDiameter)) {
        throw std::invalid_argument("The peak fit limit must not be negative");
    }
    if (!(IsPositive(shape.ejectaExponent) && std::isfinite(shape.reachRadii) &&
          shape.reachRadii > 1.0)) {
        throw std::invalid_argument(
            "Complex craters need a positive ejecta exponent and a reach over 1 radius");
    }
}

void SetKernelComplexShape(CraterProfileKernel& p, const ComplexCraterShape& shape) {
    const KilometerPowerLaw width = shape.peakDiameter ? *shape.peakDiameter : shape.peakVolume;
    p.complex = 1u;
    p.transitionDiameter = static_cast<float>(shape.transitionDiameter);
    p.floorCoefficient = static_cast<float>(shape.floorDiameter.coefficient);
    p.floorExponent = static_cast<float>(shape.floorDiameter.exponent);
    p.peakHeightCoefficient = static_cast<float>(shape.peakHeight.coefficient);
    p.peakHeightExponent = static_cast<float>(shape.peakHeight.exponent);
    p.peakWidthCoefficient = static_cast<float>(width.coefficient);
    p.peakWidthExponent = static_cast<float>(width.exponent);
    p.peakFromVolume = shape.peakDiameter ? 0u : 1u;
    p.peakFitMaxDiameter = static_cast<float>(shape.peakFitMaxDiameter);
    p.ejectaExponent = static_cast<float>(shape.ejectaExponent);
    p.complexReachRadii = static_cast<float>(shape.reachRadii);
}

ComplexCraterProfile::ComplexCraterProfile(std::shared_ptr<const CraterProfile> simple,
                                           ComplexCraterShape shape)
    : simple_(std::move(simple)), shape_(shape) {
    if (!simple_) {
        throw std::invalid_argument("A complex crater profile needs a simple one");
    }
    ValidateComplexCraterShape(shape_);
    SetKernelComplexShape(kernel_, shape_);
}

double ComplexCraterProfile::ReachRadii() const {
    return std::fmax(simple_->ReachRadii(), shape_.reachRadii);
}

double ComplexCraterProfile::Height(double distance, double diameter,
                                    const CraterState& state) const {
    if (!IsComplex(diameter)) {
        return simple_->Height(distance, diameter, state);
    }
    return static_cast<double>(ComplexProfileHeight(kernel_, static_cast<float>(distance),
                                                    static_cast<float>(diameter),
                                                    KernelStateRecord(diameter, state)));
}

}  // namespace terraingen
