#include "terraingen/layers/craters/shaping/apply_options.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace terraingen {

void ValidateCraterApplyOptions(const CraterApplyOptions& options) {
    ValidateResolutionFade(options.fade);
    ValidateCraterIrregularity(options.irregularity);
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
