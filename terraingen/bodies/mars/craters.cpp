#include "terraingen/bodies/mars/craters.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

CraterMorphometry MarsFreshCraterMorphometry(double diameter) {
    if (!(std::isfinite(diameter) && diameter > 0.0)) {
        throw std::domain_error("Diameter must be positive and finite");
    }
    if (diameter >= kMarsComplexCraterDiameter) {
        const KilometrePowerLaw rim =
            diameter > 100000.0 ? KilometrePowerLaw{0.12, 0.35} : KilometrePowerLaw{0.02, 0.84};
        return {KilometrePowerLaw{0.36, 0.49}.At(diameter) / diameter, rim.At(diameter) / diameter};
    }
    return {std::fmin(0.23, KilometrePowerLaw{0.21, 0.81}.At(diameter) / diameter),
            std::fmin(0.03, KilometrePowerLaw{0.04, 0.31}.At(diameter) / diameter)};
}

ComplexCraterShape MarsComplexCraterShape() {
    ComplexCraterShape shape;
    shape.transitionDiameter = kMarsComplexCraterDiameter;
    shape.floorDiameter = {0.187, 1.249};
    shape.peakHeight = {0.04, 0.51};
    shape.peakDiameter = KilometrePowerLaw{0.25, 1.05};
    shape.peakFitMaxDiameter = 100000.0;
    return shape;
}

}  // namespace terraingen
