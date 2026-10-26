// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/density_map.h"

#include "terraingen/util/bits.h"
#include "util/checks.h"
#include "util/numerics.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// Simpson intervals in log diameter for an areal fraction.
constexpr int kArealSteps = 2048;

/// The share of ground covered by features of `distribution` in [`low`, `high`) meters: the
/// integral of pi D^2 / 4 dN/dD, taken in log diameter.
double ArealFraction(const SizeFrequencyDistribution& distribution, double low, double high) {
    double sum = 0.0;
    ForEachLogSimpsonNode(low, high, kArealSteps, [&](double d, double weight) {
        sum += weight * 0.25 * kPi * d * d * distribution.Incremental(d);
    });
    return sum;
}

}  // namespace

void ValidateDensityMap(const DensityMap& map) {
    if (!map.field) {
        throw std::invalid_argument("A density map needs a field");
    }
    if (map.measure != DensityMeasure::Scale &&
        !(IsPositive(map.minDiameter) && map.maxDiameter > map.minDiameter)) {
        throw std::invalid_argument("A density map's diameters must satisfy 0 < min < max");
    }
    RequirePositive(map.areaUnit, "Density map areaUnit");
}

double MapMeasure(const DensityMap& map, const SizeFrequencyDistribution& distribution) {
    switch (map.measure) {
    case DensityMeasure::Scale:
        return 1.0;
    case DensityMeasure::Count:
        return distribution.ExpectedCount(1.0, map.minDiameter, map.maxDiameter);
    case DensityMeasure::ArealFraction: {
        const double high = std::fmin(map.maxDiameter, distribution.ValidRange().max);
        if (!std::isfinite(high)) {
            throw std::domain_error("An areal fraction needs a finite largest diameter");
        }
        return ArealFraction(distribution, map.minDiameter, high);
    }
    }
    return 1.0;
}

void ValidateSizeSlopeMap(const SizeSlopeMap& map) {
    if (!map.field) {
        throw std::invalid_argument("A size slope map needs a field");
    }
    RequirePositive(map.referenceDiameter, "Size slope map referenceDiameter");
    if (!(map.minExponent > 0.0 && map.maxExponent > map.minExponent &&
          std::isfinite(map.maxExponent))) {
        throw std::invalid_argument("A size slope map's exponents must satisfy 0 < min < max");
    }
}

std::optional<double> SizeSlopeAt(const SizeSlopeMap& map, double x, double y) {
    const double value = map.field->ValueAt(x, y);
    if (std::isnan(value)) {
        return std::nullopt;
    }
    double exponent = value;
    if (map.measure == SizeSlopeMeasure::MeanDiameter) {
        const double above = value - map.referenceDiameter;
        exponent = above > 0.0 ? value / above : map.maxExponent;
    }
    return std::clamp(exponent, map.minExponent, map.maxExponent);
}

double MapValueAt(const DensityMap& map, double x, double y) {
    const double value = map.field->ValueAt(x, y);
    return map.measure == DensityMeasure::Count ? value / map.areaUnit : value;
}

double MapMax(const DensityMap& map) {
    const double top = map.field->Max();
    return map.measure == DensityMeasure::Count ? top / map.areaUnit : top;
}

DensityScale::DensityScale(DensityMap map, const SizeFrequencyDistribution& distribution)
    : map_(std::move(map)) {
    ValidateDensityMap(map_);
    measure_ = MapMeasure(map_, distribution);
    if (!(measure_ > 0.0 && std::isfinite(measure_))) {
        throw std::invalid_argument("A density map must measure some of its size distribution");
    }
    max_ = std::fmax(1.0, MapMax(map_) / measure_);
}

double DensityScale::At(double x, double y) const {
    return Scale(MapValueAt(map_, x, y));
}

double DensityScale::Scale(double value) const {
    if (std::isnan(value)) {
        return 1.0;
    }
    return std::clamp(value / measure_, 0.0, max_);
}

}  // namespace terraingen
