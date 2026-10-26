// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Density maps: how a map of feature counts or cover sets a layer's local density.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/field.h"

#include <limits>
#include <memory>
#include <optional>

namespace terraingen {

/// What a density map's values measure.
enum class DensityMeasure {
    Scale,          ///< The local density over the layer's own; 1 keeps it as given.
    Count,          ///< Features in [`minDiameter`, `maxDiameter`) per `areaUnit` square meters.
    ArealFraction,  ///< Share of the ground covered by features in [`minDiameter`, `maxDiameter`).
};

/// A map of a crater or boulder population that sets a layer's density point by point: the size
/// distribution keeps its shape and is scaled to measure what the map does, and applies as given
/// where the map has no value.
struct DensityMap {
    std::shared_ptr<const SurfaceField> field;       ///< The map; not null.
    DensityMeasure measure = DensityMeasure::Scale;  ///< What its values measure.
    double minDiameter = 0.0;  ///< Smallest diameter measured (m); positive unless `Scale`.
    /// Largest diameter measured (m), exclusive; finite for `ArealFraction` unless the size
    /// distribution's valid range is.
    double maxDiameter = std::numeric_limits<double>::infinity();
    double areaUnit = 1.0;  ///< Square meters per map area unit for `Count`: 1e6 for per km^2.
    /// The map measures every feature of its kind, including those a layer adds by other means,
    /// which are taken out of it; otherwise it measures only what the layer generates.
    bool total = true;

    bool operator==(const DensityMap& other) const {
        return field == other.field && measure == other.measure &&
               minDiameter == other.minDiameter && maxDiameter == other.maxDiameter &&
               areaUnit == other.areaUnit && total == other.total;
    }
};

/// What a size slope map's values measure.
enum class SizeSlopeMeasure {
    Exponent,  ///< The cumulative exponent alpha of N(>=D), which falls as D^-alpha.
    /// The mean diameter (m) of the features at least `referenceDiameter` across, from which
    /// alpha = mean / (mean - D0) as for a power law above D0.
    MeanDiameter,
};

/// A map of how steeply feature counts fall with size, such as a mean boulder diameter map.
///
/// At each point it bends the layer's size distribution into a power law through the count the
/// layer has there at `referenceDiameter`: N(>=D) = N(>=D0) (D / D0)^-alpha. A density map that
/// counts from D0 is kept exactly; where the map has no value the sizes keep their own shape.
struct SizeSlopeMap {
    std::shared_ptr<const SurfaceField> field;              ///< The map; not null.
    SizeSlopeMeasure measure = SizeSlopeMeasure::Exponent;  ///< What its values measure.
    double referenceDiameter = 1.0;  ///< D0 (m), where the power law meets the layer's count.
    /// The exponents alpha is clamped to, which bound the features drawn; positive and below
    /// `maxExponent`. Generous bounds draw more candidates to thin.
    double minExponent = 1.0;
    double maxExponent = 6.0;  ///< The largest exponent; finite.

    bool operator==(const SizeSlopeMap& other) const {
        return field == other.field && measure == other.measure &&
               referenceDiameter == other.referenceDiameter && minExponent == other.minExponent &&
               maxExponent == other.maxExponent;
    }
};

/// Checks a size slope map.
/// @throws std::invalid_argument for no field, a reference diameter that is not positive and
/// finite, or exponents that are not 0 < min < max finite.
void ValidateSizeSlopeMap(const SizeSlopeMap& map);

/// The exponent alpha `map` gives at surface (x, y), clamped to its bounds; none where it has no
/// value.
std::optional<double> SizeSlopeAt(const SizeSlopeMap& map, double x, double y);

/// Checks a density map.
/// @throws std::invalid_argument for no field, a diameter range that is not 0 < min < max for a
/// `Count` or `ArealFraction`, or an area unit that is not positive and finite.
void ValidateDensityMap(const DensityMap& map);

/// What `map` measures of `distribution` as it is: its count per square meter in the map's range,
/// the share of ground its features there cover, or 1 for `Scale`.
/// @throws std::domain_error if the range leaves the distribution's valid range, or an
/// `ArealFraction` has no finite largest diameter.
double MapMeasure(const DensityMap& map, const SizeFrequencyDistribution& distribution);

/// The map's value at surface (x, y) in square meters for a `Count`; NaN where it has none.
double MapValueAt(const DensityMap& map, double x, double y);

/// The largest value the map reads anywhere, in the units of `MapValueAt`.
double MapMax(const DensityMap& map);

/// The local scale a density map gives a size distribution: the map's value over its measure of
/// the distribution, 1 where the map has no value.
class DensityScale {
  public:
    /// @throws std::invalid_argument for an invalid map or one measuring nothing of
    /// `distribution`.
    /// @throws std::domain_error as `MapMeasure`.
    DensityScale(DensityMap map, const SizeFrequencyDistribution& distribution);

    /// The scale at surface (x, y), not negative.
    double At(double x, double y) const;

    /// The scale where the map reads `value` (`MapValueAt`), clamped to [0, `Max`]; 1 for NaN.
    double Scale(double value) const;

    /// What the map measures of the distribution at a scale of 1.
    double Measure() const {
        return measure_;
    }

    /// The largest scale anywhere, at least 1.
    double Max() const {
        return max_;
    }

    /// The map.
    const DensityMap& Map() const {
        return map_;
    }

  private:
    DensityMap map_;
    double measure_ = 1.0;
    double max_ = 1.0;
};

}  // namespace terraingen
