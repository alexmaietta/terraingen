#pragma once

#include "terraingen/layers/craters/apply.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/layer.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace terraingen {

/// The largest secondary crater (m) at `range` metres from the centre of a primary `diameter`
/// metres across, after the generalized fit of Singer et al. (2020, Table 3) to six lunar secondary
/// fields from 0.83 to 660 km: d = a R^-b with a = 0.022 D^1.96 and -b = -0.14 ln D, D and R in km.
/// They find it within about +-50% of each field's 99th-percentile envelope. Singer et al. measure
/// range from the primary; it is taken here from its centre.
///
/// Reference: K. N. Singer, B. L. Jolliff and W. B. McKinnon, "Lunar secondary craters and
/// estimated ejecta block sizes reveal a scale-dependent fragmentation trend", JGR Planets 125,
/// e2019JE006313, 2020. https://doi.org/10.1029/2019JE006313
double SingerMaxSecondaryDiameter(double diameter, double range);

/// How far from its centre (m) a primary `diameter` metres across throws its first secondaries:
/// its radius plus `rimDiameters` diameters, which Singer et al. (2020) find about 1.3 for
/// primaries of 31 km and more (Orientale, Copernicus, Kepler) and about 1.8 for those of 0.8 to 3
/// km; between 3 and 31 km it is interpolated linearly in log diameter, and held beyond the
/// measured sizes.
double SecondaryOnsetRange(double diameter);

/// Settings for a `SecondaryCraterLayer`: which primaries throw secondaries, how many, and over
/// what sizes.
///
/// At range R from a primary of diameter D, the secondaries at least d across number, per square
/// metre, n(R) (d / (`countFraction` D))^-`sizeSlope` for d below Singer et al.'s largest secondary
/// there (`SingerMaxSecondaryDiameter`) and none above it, between `SecondaryOnsetRange` and an
/// outer range (`outerRange`, or by default the longest typical ray of Elliott et al. (2018), 1.6
/// times their median ray length 6.59 (D / 2)^1.27 km). Following Chapman's (1968) model of
/// secondary families, n falls as r^-2 with r = 1 + (R - onset) / D, and the cumulative size slope
/// is about -4 (Shoemaker 1965, as Chapman cites it). How many secondaries there are is not fixed
/// by these sources: `onsetCount`, the number at least `countFraction` D across in an area the size
/// of the primary at the onset, must be given (Chapman's own provisional value, about 3 between
/// D/25 and D/15, overpredicts the craters he observed about 150-fold).
///
/// Secondaries take their primary's age, are shaped by the layer's own profile and degradation,
/// and are placed uniformly in direction; their alignment along rays and radial elongation are not
/// modelled.
///
/// References:
/// - K. N. Singer, B. L. Jolliff and W. B. McKinnon, JGR Planets 125, e2019JE006313, 2020.
/// - C. R. Chapman, "Interpretation of the diameter-frequency relation for lunar craters
///   photographed by Rangers VII, VIII, and IX", Icarus 8:1-22, 1968.
///   https://doi.org/10.1016/0019-1035(68)90058-4
/// - J. R. Elliott, Y.-H. Huang, D. A. Minton and A. M. Freed, "The length of lunar crater rays
///   explained using secondary crater scaling", Icarus 312:231-246, 2018.
///   https://doi.org/10.1016/j.icarus.2018.04.015
struct SecondaryCraterOptions {
    std::uint32_t seed = 0;
    std::optional<double> onsetCount;
    double countFraction = 0.01;
    double sizeSlope = 4.0;
    /// Diameters of secondaries generated (m).
    double minDiameter = 1.0;
    double maxDiameter = std::numeric_limits<double>::infinity();
    /// Outer range in primary diameters from the centre; unset uses Elliott et al.'s rays.
    std::optional<double> outerRange;
    /// Primaries that throw secondaries: diameters (m) and ages (Gyr) within these, and manual
    /// craters only with `manual`.
    double minPrimaryDiameter = 0.0;
    double maxPrimaryDiameter = std::numeric_limits<double>::infinity();
    double minAgeGyr = 0.0;
    double maxAgeGyr = std::numeric_limits<double>::infinity();
    bool manual = true;
    /// Polar cell side as a multiple of each size band's largest diameter.
    double cellSizeFactor = 16.0;
    /// Most secondaries expected over one region, to catch requests that would exhaust memory.
    double maxExpected = 5e7;

    bool operator==(const SecondaryCraterOptions&) const = default;
};

/// @throws std::invalid_argument without a positive `onsetCount`, for a non-positive count
/// fraction or size slope, an empty or negative diameter range, a non-positive outer range,
/// inverted or negative primary size or age ranges, or a non-positive cell size factor or limit.
void ValidateSecondaryCraterOptions(const SecondaryCraterOptions& options);

/// The models secondaries are shaped with.
struct SecondaryCraterModels {
    std::shared_ptr<const CraterProfile> profile;
    std::shared_ptr<const DegradationModel> degradation;
};

/// Secondary craters around the craters of a `CraterLayer`, as `SecondaryCraterOptions` describes,
/// applied with `ApplyCraters` (additively; datums are not supported). They are drawn per primary,
/// in polar cells keyed to its ID, size band, ring and sector, so a tile holds the same secondaries
/// as any larger grid; bands too small for the sample spacing are skipped, as for primaries.
class SecondaryCraterLayer final : public Layer {
  public:
    /// @throws std::invalid_argument for a null layer or model, invalid options, or apply options
    /// with a datum.
    SecondaryCraterLayer(std::shared_ptr<const CraterLayer> primaries, SecondaryCraterModels models,
                         SecondaryCraterOptions options, CraterApplyOptions apply = {});

    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    double MaxFeatureDiameter() const override;

    /// The secondaries a grid covering `region` gets at sample spacing `spacing` (0 for every
    /// size): those centred in it or within the profile's reach of it.
    /// @throws std::invalid_argument if more than `maxExpected` are expected.
    CraterList Secondaries(const Bounds2d& region, double spacing = 0.0) const;

    /// The outer range (m from the centre) of a primary `diameter` across.
    double OuterRange(double diameter) const;

    const SecondaryCraterOptions& Options() const {
        return options_;
    }

  private:
    std::shared_ptr<const CraterLayer> primaries_;
    SecondaryCraterModels models_;
    SecondaryCraterOptions options_;
    CraterApplyOptions apply_;
};

}  // namespace terraingen
