/// @file
/// @brief Density maps: how a map of feature counts or cover sets a layer's local density.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/sources/field.h"

#include <limits>
#include <memory>

namespace terraingen {

/// What a density map's values measure.
enum class DensityMeasure {
    Scale,          ///< The local density over the layer's own; 1 keeps it as given.
    Count,          ///< Features in [`minDiameter`, `maxDiameter`) per `areaUnit` square metres.
    ArealFraction,  ///< Share of the ground covered by features in [`minDiameter`, `maxDiameter`).
};

/// A map of a crater or boulder population that sets a layer's density point by point.
///
/// The layer's size distribution keeps its shape and is scaled at each point until it measures
/// what the map does, so a map of the larger, detectable features also sets the smaller ones
/// below its detection limit. Features the layer places by other means are not counted twice:
/// manual catalog features stand in for generated ones above the catalog's completeness limit
/// (`CraterGenerationOptions::completeness`), and with `total` a boulder layer takes the blocks
/// it throws out around craters out of the map's measure first. Where the map has no value the
/// layer's density applies as given.
struct DensityMap {
    std::shared_ptr<const SurfaceField> field;       ///< The map; not null.
    DensityMeasure measure = DensityMeasure::Scale;  ///< What its values measure.
    double minDiameter = 0.0;  ///< Smallest diameter measured (m); positive unless `Scale`.
    /// Largest diameter measured (m), exclusive; finite for `ArealFraction` unless the size
    /// distribution's valid range is.
    double maxDiameter = std::numeric_limits<double>::infinity();
    double areaUnit = 1.0;  ///< Square metres per map area unit for `Count`: 1e6 for per km^2.
    /// The map measures every feature of its kind, including those a layer adds by other means,
    /// which are taken out of it; otherwise it measures only what the layer generates.
    bool total = true;

    bool operator==(const DensityMap&) const = default;
};

/// Checks a density map.
/// @throws std::invalid_argument for no field, a diameter range that is not 0 < min < max for a
/// `Count` or `ArealFraction`, or an area unit that is not positive and finite.
void ValidateDensityMap(const DensityMap& map);

/// What `map` measures of `distribution` as it is: its count per square metre in the map's range,
/// the share of ground its features there cover, or 1 for `Scale`.
/// @throws std::domain_error if the range leaves the distribution's valid range, or an
/// `ArealFraction` has no finite largest diameter.
double MapMeasure(const DensityMap& map, const SizeFrequencyDistribution& distribution);

/// The map's value at surface (x, y) in square metres for a `Count`; NaN where it has none.
double MapValueAt(const DensityMap& map, double x, double y);

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

/// The share of features drawn at a `DensityScale`'s largest scale that its local scale keeps,
/// in [0, 1], for `CraterGenerationOptions::keepShare`.
class DensityKeepShare final : public SurfaceField {
  public:
    explicit DensityKeepShare(std::shared_ptr<const DensityScale> scale);

    double ValueAt(double x, double y) const override;
    double Max() const override {
        return 1.0;
    }

  private:
    std::shared_ptr<const DensityScale> scale_;
};

}  // namespace terraingen
