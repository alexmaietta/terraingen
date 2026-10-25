// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/shaping/apply_options.h"

#include "terraingen/random/stream.h"
#include "terraingen/random/streams.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <stdexcept>

namespace terraingen {

void ValidateRimSmoothing(const RimSmoothing& smoothing) {
    RequireNonNegative(smoothing.sigmaRadii, "Rim smoothing sigma");
    RequireUnitInterval(smoothing.strength, "Rim smoothing strength");
    if (!(smoothing.innerRadii >= 0.0 && smoothing.innerRadii < 1.0 && smoothing.outerRadii > 1.0 &&
          std::isfinite(smoothing.outerRadii))) {
        throw std::invalid_argument("Rim smoothing band must satisfy 0 <= inner < 1 < outer");
    }
}

void ValidateCraterTerraces(const CraterTerraces& terraces) {
    RequirePositive(terraces.onsetDiameter, "Crater terrace onset diameter");
    if (!(terraces.fullDiameter > terraces.onsetDiameter && std::isfinite(terraces.fullDiameter))) {
        throw std::invalid_argument(
            "Crater terrace full diameter must be finite and above the onset diameter");
    }
    if (!(terraces.minBenches >= 1 && terraces.minBenches <= terraces.maxBenches &&
          terraces.maxBenches <= 12)) {
        throw std::invalid_argument("Crater terrace benches must satisfy 1 <= min <= max <= 12");
    }
    if (!(terraces.benchSlopeDegrees > 0.0 && terraces.benchSlopeDegrees < 45.0)) {
        throw std::invalid_argument("Crater terrace bench slope must be in (0, 45) degrees");
    }
    if (!(terraces.scarpShare > 0.0 && terraces.scarpShare < 1.0)) {
        throw std::invalid_argument("Crater terrace scarp share must be in (0, 1)");
    }
    if (!(terraces.arcShare > 0.0 && terraces.arcShare <= 1.0)) {
        throw std::invalid_argument("Crater terrace arc share must be in (0, 1]");
    }
    RequireNonNegative(terraces.rimVariation, "Crater terrace rim variation");
    RequireUnitInterval(terraces.blockOffset, "Crater terrace block offset");
    if (!(terraces.blockGaps >= 0.0 && terraces.blockGaps < 1.0)) {
        throw std::invalid_argument("Crater terrace block gaps must be in [0, 1)");
    }
    if (!(terraces.minFreshness >= 0.0 && terraces.minFreshness < 1.0)) {
        throw std::invalid_argument("Crater terrace minimum freshness must be in [0, 1)");
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

void ValidateCraterTexture(const CraterTexture& texture) {
    RequireNonNegative(texture.wallStreaks, "Crater wall streak amplitude");
    RequireNonNegative(texture.ejecta, "Crater ejecta texture amplitude");
    RequirePositive(texture.wallStreakWidthRadii, "Crater wall streak width");
    RequirePositive(texture.ejectaLineationRadii, "Crater ejecta lineation width");
    RequireUnitInterval(texture.mantling, "Crater ejecta mantling");
    if (!(texture.ejectaReachRadii > 1.0 && std::isfinite(texture.ejectaReachRadii))) {
        throw std::invalid_argument("Crater ejecta must reach beyond 1 radius");
    }
    if (!(texture.minFreshness >= 0.0 && texture.minFreshness < 1.0)) {
        throw std::invalid_argument("Crater texture minFreshness must be in [0, 1)");
    }
}

CraterApplyOptions DefaultCraterApply() {
    CraterApplyOptions apply;
    apply.irregularity.enabled = true;
    apply.terraces.enabled = true;
    return apply;
}

CraterApplyOptions SeededCraterApply(CraterApplyOptions apply, std::uint32_t seed) {
    const auto follow = [seed](std::optional<std::uint32_t>& own, std::uint32_t stream) {
        if (!own) {
            own = SubstreamSeed(seed, stream);
        }
    };
    follow(apply.irregularity.seed, kCraterIrregularityStream);
    follow(apply.noise.seed, kCraterNoiseStream);
    follow(apply.texture.seed, kCraterTextureStream);
    follow(apply.slope.seed, kCraterSlopeStream);
    follow(apply.terraces.seed, kCraterTerraceStream);
    return apply;
}

void ValidateCraterApplyOptions(const CraterApplyOptions& options) {
    ValidateCraterNoise(options.noise);
    ValidateCraterTexture(options.texture);
    ValidateResolutionFade(options.fade);
    ValidateCraterIrregularity(options.irregularity);
    ValidateRimSmoothing(options.rimSmoothing);
    ValidateCraterSlopeResponse(options.slope);
    ValidateCraterTerraces(options.terraces);
    if ((options.slope.asymmetric || options.slope.criticalGradient) &&
        options.datum != CraterDatum::Plane) {
        throw std::invalid_argument("A crater slope response needs the plane datum");
    }
    RequirePositive(options.datumFitRadii, "datumFitRadii");
    RequireUnitInterval(options.interiorReplacement, "interiorReplacement");
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
