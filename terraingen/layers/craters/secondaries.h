/// @file
/// @brief Secondary craters around the craters of a `CraterLayer`, and the laws they follow.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/crater_placer.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/layer.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace terraingen {

/// The largest secondary crater's diameter at `range` metres from a primary of `diameter` metres,
/// in metres, after the generalized fit of Singer et al. (2020): d = a R^-b with a = 0.022 D^1.96
/// and b = 0.14 ln D, D and R in km.
double SingerMaxSecondaryDiameter(double diameter, double range);

/// The range in metres beyond which `SingerMaxSecondaryDiameter` falls below `size` metres: its
/// inverse in range. Infinite for primaries of 1 km or less, whose largest secondary does not
/// shrink with range.
double SingerRangeForSize(double diameter, double size);

/// How far from its centre a primary of `diameter` metres throws its first secondaries (Singer et
/// al. 2020): the radius plus about 1.8 diameters up to 3 km and 1.3 from 31 km, in metres.
double SecondaryOnsetRange(double diameter);

/// The median ray length in metres of Elliott et al. (2018, eq. 2): `coefficient` R^`exponent` km
/// for a crater of radius R km.
/// @param diameter In metres.
/// @param coefficient In km.
double ElliottMedianRayLength(double diameter, double coefficient = 6.59, double exponent = 1.27);

/// Which primaries throw secondaries, how many, and over what sizes.
///
/// At range R from a primary of diameter D, secondaries at least d across number
/// n(R) (d / (`countFraction` D))^-`sizeSlope` per m^2, below `SingerMaxSecondaryDiameter` and
/// between `SecondaryOnsetRange` and the outer range.
struct SecondaryCraterOptions {
    std::uint32_t seed = 0;  ///< Varies the secondaries.
    /// Secondaries at least `countFraction` D across in an area the size of the primary at the
    /// onset; required, as the sources fix sizes but not numbers.
    std::optional<double> onsetCount;
    double countFraction = 0.01;  ///< Reference size of `onsetCount`, over D.
    double sizeSlope = 4.0;       ///< Cumulative size slope, positive.
    double minDiameter = 1.0;     ///< Smallest secondary generated (m).
    double maxDiameter = std::numeric_limits<double>::infinity();  ///< Largest (m), exclusive.
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

/// Checks secondary crater options.
/// @throws std::invalid_argument without a positive `onsetCount`, for a non-positive count
/// fraction or size slope, an empty or negative diameter range, a non-positive outer range, an
/// invalid selection, or a non-positive cell size factor or limit.
void ValidateSecondaryCraterOptions(const SecondaryCraterOptions& options);

/// Secondary craters around the craters of a `CraterLayer`, applied additively with
/// `ApplyCraters`.
///
/// Drawn per primary in polar cells keyed to its ID, size band, ring and sector, so a tile holds
/// the same secondaries as any larger grid; bands too small for the spacing are skipped.
class SecondaryCraterLayer final : public Layer, public CraterPlacer {
  public:
    /// @param primaries The layer whose craters throw secondaries.
    /// @param apply How secondaries are applied; without a datum.
    /// @throws std::invalid_argument for a null layer or model, invalid options, or apply options
    /// with a datum.
    SecondaryCraterLayer(std::shared_ptr<const CraterLayer> primaries, CraterShapeModels models,
                         SecondaryCraterOptions options, CraterApplyOptions apply = {});

    /// The largest secondary the layer places, in metres.
    double MaxFeatureDiameter() const override;

    /// The secondaries a grid covering `region` (surface coordinates) gets: those centred in it or
    /// within the profile's reach of it.
    /// @param spacing Sample spacing in metres; 0 for every size.
    /// @throws std::invalid_argument if more than `maxExpected` are expected.
    CraterList Secondaries(const Bounds2d& region, double spacing = 0.0) const;

    /// The outer range from its centre, in metres, of the secondaries of a primary of `diameter`
    /// metres.
    double OuterRange(double diameter) const;

    /// A secondary's attributes in the state the degradation model gives it.
    /// @throws std::domain_error as `DegradationModel::State`.
    CraterAttributes Attributes(const Crater& crater) const;

    CraterFeatures CratersOver(const RasterGrid& grid, double spacing) const override;

    /// The options the layer was made with.
    const SecondaryCraterOptions& Options() const {
        return options_;
    }

  protected:
    /// Applies the secondaries reaching the samples.
    /// @throws std::invalid_argument if more than `maxExpected` secondaries are expected.
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    std::shared_ptr<const CraterLayer> primaries_;
    CraterShapeModels models_;
    SecondaryCraterOptions options_;
    CraterApplyOptions apply_;
};

}  // namespace terraingen
