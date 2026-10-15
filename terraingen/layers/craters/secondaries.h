/// @file
/// @brief Secondary craters around the craters of a `CraterLayer`, and the size and range laws
/// they follow.
#pragma once

#include "terraingen/layers/craters/apply.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/selection.h"
#include "terraingen/layers/layer.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace terraingen {

/// @brief The largest secondary crater at a range from a primary, after the generalized fit of
/// Singer et al. (2020): d = a R^-b with a = 0.022 D^1.96 and b = 0.14 ln D, D and R in km.
/// See SCIENCE.md, "Secondary craters".
/// @param diameter The primary's diameter, in metres.
/// @param range The distance from the primary's centre, in metres.
/// @return The largest secondary's diameter, in metres.
double SingerMaxSecondaryDiameter(double diameter, double range);

/// @brief The range beyond which `SingerMaxSecondaryDiameter` falls below a size: its inverse in
/// range.
/// @param diameter The primary's diameter, in metres.
/// @param size The secondary's diameter, in metres.
/// @return The range, in metres; infinite for primaries of 1 km or less, whose largest secondary
/// does not shrink with range.
double SingerRangeForSize(double diameter, double size);

/// @brief How far from its centre a primary throws its first secondaries: its radius plus about
/// 1.8 diameters for primaries up to 3 km and 1.3 from 31 km, interpolated in log diameter
/// between (Singer et al. 2020).
/// @param diameter The primary's diameter, in metres.
/// @return The onset range, in metres.
double SecondaryOnsetRange(double diameter);

/// @brief The median ray length of Elliott et al. (2018, eq. 2): `coefficient` R^`exponent` km
/// for a crater of radius R km.
/// @param diameter The crater's diameter, in metres.
/// @param coefficient The law's coefficient, in km.
/// @param exponent The law's exponent.
/// @return The median ray length, in metres.
double ElliottMedianRayLength(double diameter, double coefficient = 6.59, double exponent = 1.27);

/// Which primaries throw secondaries, how many, and over what sizes.
///
/// At range R from a primary of diameter D, the secondaries at least d across number, per square
/// metre, n(R) (d / (`countFraction` D))^-`sizeSlope` below `SingerMaxSecondaryDiameter`, between
/// `SecondaryOnsetRange` and an outer range, with n falling as r^-2 for r = 1 + (R - onset) / D.
/// `onsetCount`, the number at least `countFraction` D across in an area the size of the primary
/// at the onset, must be given: the sources fix the sizes, not the number. Secondaries take their
/// primary's age and are placed uniformly in direction. See SCIENCE.md, "Secondary craters".
struct SecondaryCraterOptions {
    std::uint32_t seed = 0;
    std::optional<double> onsetCount;
    double countFraction = 0.01;
    double sizeSlope = 4.0;
    /// Diameters of secondaries generated (m).
    double minDiameter = 1.0;
    double maxDiameter = std::numeric_limits<double>::infinity();
    /// Outer range in primary diameters from the centre; unset uses 1.6 times Elliott et al.'s
    /// median ray length (`ElliottMedianRayLength`).
    std::optional<double> outerRange;
    /// The primaries that throw secondaries.
    CraterSelection selection{};
    /// Polar cell side as a multiple of each size band's largest diameter.
    double cellSizeFactor = 16.0;
    /// Most secondaries expected over one region, to catch requests that would exhaust memory.
    double maxExpected = 5e7;

    bool operator==(const SecondaryCraterOptions&) const = default;
};

/// @brief Checks secondary crater options.
/// @param options The options to check.
/// @throws std::invalid_argument without a positive `onsetCount`, for a non-positive count
/// fraction or size slope, an empty or negative diameter range, a non-positive outer range, an
/// invalid selection, or a non-positive cell size factor or limit.
void ValidateSecondaryCraterOptions(const SecondaryCraterOptions& options);

/// Secondary craters around the craters of a `CraterLayer`, as `SecondaryCraterOptions` describes,
/// applied with `ApplyCraters` (additively; datums are not supported). They are drawn per primary,
/// in polar cells keyed to its ID, size band, ring and sector, so a tile holds the same secondaries
/// as any larger grid; bands too small for the sample spacing are skipped, as for primaries.
class SecondaryCraterLayer final : public Layer {
  public:
    /// @brief Builds a secondary crater layer.
    /// @param primaries The layer whose craters throw secondaries.
    /// @param models The profile and degradation secondaries are shaped with.
    /// @param options Which primaries throw secondaries, and how many.
    /// @param apply How secondaries are applied; without a datum.
    /// @throws std::invalid_argument for a null layer or model, invalid options, or apply options
    /// with a datum.
    SecondaryCraterLayer(std::shared_ptr<const CraterLayer> primaries, CraterShapeModels models,
                         SecondaryCraterOptions options, CraterApplyOptions apply = {});

    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// @throws std::invalid_argument as `ValidatePointContext`.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    double MaxFeatureDiameter() const override;

    /// @brief The secondaries a grid covering a region gets: those centred in it or within the
    /// profile's reach of it.
    /// @param region The region, in surface coordinates.
    /// @param spacing The sample spacing; 0 for every size.
    /// @return The secondaries.
    /// @throws std::invalid_argument if more than `maxExpected` are expected.
    CraterList Secondaries(const Bounds2d& region, double spacing = 0.0) const;

    /// @brief The outer range of a primary's secondaries.
    /// @param diameter The primary's diameter, in metres.
    /// @return The range from its centre, in metres.
    double OuterRange(double diameter) const;

    const SecondaryCraterOptions& Options() const {
        return options_;
    }

  private:
    std::shared_ptr<const CraterLayer> primaries_;
    CraterShapeModels models_;
    SecondaryCraterOptions options_;
    CraterApplyOptions apply_;
};

}  // namespace terraingen
