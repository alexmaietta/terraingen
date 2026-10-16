/// @file
/// @brief Fresh lunar crater morphometry and lunar topographic diffusivities.
#include "terraingen/bodies/moon/craters.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

double LunarFreshCraterDepthRatio(double diameter) {
    if (!IsPositive(diameter)) {
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

CraterMorphometry LunarFreshCraterMorphometry(double diameter) {
    if (diameter >= kLunarComplexCraterDiameter && std::isfinite(diameter)) {
        const KilometrePowerLaw depth{1.044, 0.301};
        const KilometrePowerLaw rim{0.236, 0.399};
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

PowerLawDiffusivity FassettThomson2014Diffusivity() {
    return {5.5, 0.0, 0.0};
}

PowerLawDiffusivity Fassett2022HartmannDiffusivity() {
    return {3.6, 0.87, 5.0};
}

PowerLawDiffusivity Fassett2022TraskDiffusivity() {
    return {3.1, 1.05, 5.0};
}

double FassettThomson2014DiffusionAge(double ageGyr) {
    const double t = ageGyr;
    const double kappaT =
        (((435.83 * t - 3621.2) * t + 11204.0) * t - 16811.0) * t * t + 17546.0 * t;
    return std::fmax(0.0, kappaT);
}

}  // namespace terraingen
