// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/shaping/morphometry.h"

#include "terraingen/util/checks.h"

#include <cmath>

namespace terraingen {

double LunarFreshCraterDepthRatio(double diameter) {
    RequireDiameter(diameter);
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

CraterMorphometry LunarFreshCraterMorphometry(double diameter) {
    if (diameter >= kLunarComplexCraterDiameter && std::isfinite(diameter)) {
        const KilometerPowerLaw depth{1.044, 0.301};
        const KilometerPowerLaw rim{0.236, 0.399};
        return {depth.At(diameter) / diameter, rim.At(diameter) / diameter};
    }
    return {LunarFreshCraterDepthRatio(diameter), kLunarFreshRimHeightRatio};
}

ComplexCraterShape LunarComplexCraterShape() {
    ComplexCraterShape shape;
    shape.transitionDiameter = kLunarComplexCraterDiameter;
    shape.floorDiameter = {0.187, 1.249};
    shape.peakHeight = {0.589e-3, 1.969};
    shape.peakVolume = {0.987e-7, 5.078};
    shape.peakFitMaxDiameter = 51000.0;
    return shape;
}

CraterMorphometry MarsFreshCraterMorphometry(double diameter) {
    RequireDiameter(diameter);
    if (diameter >= kMarsComplexCraterDiameter) {
        const KilometerPowerLaw rim =
            diameter > 100000.0 ? KilometerPowerLaw{0.12, 0.35} : KilometerPowerLaw{0.02, 0.84};
        return {KilometerPowerLaw{0.36, 0.49}.At(diameter) / diameter, rim.At(diameter) / diameter};
    }
    return {std::fmin(0.23, KilometerPowerLaw{0.21, 0.81}.At(diameter) / diameter),
            std::fmin(0.03, KilometerPowerLaw{0.04, 0.31}.At(diameter) / diameter)};
}

ComplexCraterShape MarsComplexCraterShape() {
    ComplexCraterShape shape;
    shape.transitionDiameter = kMarsComplexCraterDiameter;
    shape.floorDiameter = {0.187, 1.249};
    shape.peakHeight = {0.04, 0.51};
    shape.peakDiameter = KilometerPowerLaw{0.25, 1.05};
    shape.peakFitMaxDiameter = 100000.0;
    return shape;
}

}  // namespace terraingen
