#include "terraingen/layers/density_map.h"

#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"

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
    const double a = std::log(low);
    const double h = (std::log(high) - a) / kArealSteps;
    const auto f = [&](int i) {
        const double d = std::exp(a + h * i);
        return 0.25 * kPi * d * d * d * distribution.Incremental(d);
    };
    double sum = f(0) + f(kArealSteps);
    for (int i = 1; i < kArealSteps; ++i) {
        sum += (i % 2 == 1 ? 4.0 : 2.0) * f(i);
    }
    return sum * h / 3.0;
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

DensityScale::DensityScale(DensityMap map, const SizeFrequencyDistribution& distribution)
    : map_(std::move(map)) {
    ValidateDensityMap(map_);
    measure_ = MapMeasure(map_, distribution);
    if (!(measure_ > 0.0 && std::isfinite(measure_))) {
        throw std::invalid_argument("A density map must measure some of its size distribution");
    }
    const double top = map_.field->Max();
    const double largest = map_.measure == DensityMeasure::Count ? top / map_.areaUnit : top;
    if (largest / measure_ > max_) {
        max_ = largest / measure_;
    }
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
