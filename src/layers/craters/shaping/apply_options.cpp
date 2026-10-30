// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/shaping/apply_options.h"

#include "terraingen/layers/craters/crater_fields.h"
#include "terraingen/random/stream.h"
#include "terraingen/random/streams.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace terraingen {

void ValidateRimSmoothing(const RimSmoothing& smoothing) {
    CheckRanges(smoothing);
}

void ValidateCraterTerraces(const CraterTerraces& terraces) {
    CheckRanges(terraces);
    if (!(terraces.fullDiameter > terraces.onsetDiameter)) {
        throw std::invalid_argument(
            "Crater terrace full diameter must be above the onset diameter");
    }
    if (terraces.minBenches > terraces.maxBenches) {
        throw std::invalid_argument("Crater terrace min_benches must be at most max_benches");
    }
    if (!(terraces.scarpSlopeDegrees > terraces.benchSlopeDegrees)) {
        throw std::invalid_argument("Crater terrace scarp slope must be above the bench slope");
    }
}

void ValidateCraterPeaks(const CraterPeaks& peaks) {
    CheckRanges(peaks);
    if (peaks.minMassifs > peaks.maxMassifs) {
        throw std::invalid_argument("Crater peak min_massifs must be at most max_massifs");
    }
}

void ValidateCraterNoise(const CraterNoise& noise) {
    CheckRanges(noise);
}

void ValidateCraterTexture(const CraterTexture& texture) {
    CheckRanges(texture);
}

CraterApplyOptions DefaultCraterApply() {
    CraterApplyOptions apply;
    apply.irregularity.enabled = true;
    apply.terraces.enabled = true;
    apply.peaks.enabled = true;
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
    follow(apply.peaks.seed, kCraterPeakStream);
    return apply;
}

void ValidateCraterReplacement(const CraterReplacement& replacement) {
    CheckRanges(replacement);
    if (replacement.endRadii && !(*replacement.endRadii > replacement.startRadii)) {
        throw std::invalid_argument("Crater replacement end_radii must be above start_radii");
    }
}

void ValidateCraterApplyOptions(const CraterApplyOptions& options) {
    CheckRanges(options);
    ValidateResolutionFade(options.fade);
    ValidateCraterIrregularity(options.irregularity);
    ValidateCraterSlopeResponse(options.slope);
    ValidateCraterTerraces(options.terraces);
    ValidateCraterPeaks(options.peaks);
    ValidateCraterReplacement(options.replacement);
    if ((options.slope.asymmetric || options.slope.criticalGradient) &&
        options.datum != CraterDatum::Plane) {
        throw std::invalid_argument("A crater slope response needs the plane datum");
    }
    if (!(options.blend.endRadii > options.blend.startRadii)) {
        throw std::invalid_argument("Crater blend end_radii must be above start_radii");
    }
}

std::vector<std::size_t> CraterApplyOrder(const CraterList& craters, CraterOrder order) {
    struct Key {
        double age;
        double diameter;
        std::uint64_t id;
        std::size_t index;
    };
    const auto& age = craters.AgesGyr();
    const auto& diameter = craters.Diameters();
    const auto& id = craters.Ids();
    std::vector<Key> keys(craters.Size());
    for (std::size_t n = 0; n < keys.size(); ++n) {
        keys[n] = {age[n], diameter[n], id[n], n};
    }
    const bool oldestFirst = order == CraterOrder::OldestFirst;
    std::sort(keys.begin(), keys.end(), [oldestFirst](const Key& a, const Key& b) {
        if (oldestFirst && a.age != b.age) {
            return a.age > b.age;
        }
        if (a.diameter != b.diameter) {
            return a.diameter > b.diameter;
        }
        if (a.age != b.age) {
            return a.age > b.age;
        }
        return a.id != b.id ? a.id < b.id : a.index < b.index;
    });
    std::vector<std::size_t> indices(keys.size());
    for (std::size_t n = 0; n < keys.size(); ++n) {
        indices[n] = keys[n].index;
    }
    return indices;
}

}  // namespace terraingen
