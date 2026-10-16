#include "terraingen/layers/craters/shaping/apply_options.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace terraingen {

double RimSmoothing::Weight(double radii) const {
    if (sigmaRadii == 0.0) {
        return 0.0;
    }
    if (radii <= 1.0) {
        return strength * Falloff(2.0 - radii, 1.0, 2.0 - innerRadii, shape);
    }
    return strength * Falloff(radii, 1.0, outerRadii, shape);
}

void ValidateRimSmoothing(const RimSmoothing& smoothing) {
    RequireNonNegative(smoothing.sigmaRadii, "Rim smoothing sigma");
    if (!(smoothing.strength >= 0.0 && smoothing.strength <= 1.0)) {
        throw std::invalid_argument("Rim smoothing strength must be between 0 and 1");
    }
    if (!(smoothing.innerRadii >= 0.0 && smoothing.innerRadii < 1.0 && smoothing.outerRadii > 1.0 &&
          std::isfinite(smoothing.outerRadii))) {
        throw std::invalid_argument("Rim smoothing band must satisfy 0 <= inner < 1 < outer");
    }
}

void ValidateCraterNoise(const CraterNoise& noise) {
    RequireNonNegative(noise.amplitude, "Crater noise amplitude");
    RequireNonNegative(noise.rimAmplitude, "Crater rim noise amplitude");
    RequirePositive(noise.wavelengthRadii, "Crater noise wavelength");
    RequirePositive(noise.rimWavelengthRadii, "Crater rim noise wavelength");
    RequirePositive(noise.gain, "Crater noise gain");
    if (noise.octaves < 1 || noise.octaves > 12) {
        throw std::invalid_argument("Crater noise needs 1 to 12 octaves");
    }
    if (!(noise.rimInnerRadii >= 0.0 && noise.rimInnerRadii < 1.0 && noise.rimOuterRadii > 1.0 &&
          std::isfinite(noise.rimOuterRadii))) {
        throw std::invalid_argument("Crater rim noise band must satisfy 0 <= inner < 1 < outer");
    }
}

void ValidateCraterApplyOptions(const CraterApplyOptions& options) {
    ValidateCraterNoise(options.noise);
    ValidateResolutionFade(options.fade);
    ValidateCraterIrregularity(options.irregularity);
    ValidateRimSmoothing(options.rimSmoothing);
    RequirePositive(options.datumFitRadii, "datumFitRadii");
    if (!(options.interiorReplacement >= 0.0 && options.interiorReplacement <= 1.0)) {
        throw std::invalid_argument("interiorReplacement must be between 0 and 1");
    }
    const CraterBlend& blend = options.blend;
    if (!(std::isfinite(blend.startRadii) && std::isfinite(blend.endRadii) &&
          blend.startRadii >= 0.0 && blend.endRadii > blend.startRadii)) {
        throw std::invalid_argument("Crater blend must satisfy 0 <= startRadii < endRadii");
    }
}

std::vector<std::size_t> CraterApplyOrder(const CraterList& craters, CraterOrder order) {
    std::vector<std::size_t> indices(craters.Size());
    std::iota(indices.begin(), indices.end(), std::size_t{0});
    const auto& age = craters.AgesGyr();
    const auto& diameter = craters.Diameters();
    const auto& id = craters.Ids();
    std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
        if (order == CraterOrder::OldestFirst && age[a] != age[b]) {
            return age[a] > age[b];
        }
        if (diameter[a] != diameter[b]) {
            return diameter[a] > diameter[b];
        }
        if (age[a] != age[b]) {
            return age[a] > age[b];
        }
        return id[a] < id[b];
    });
    return indices;
}

}  // namespace terraingen
