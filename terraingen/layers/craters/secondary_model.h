/// @file
/// @brief The laws secondary craters follow, their options, and their density around a primary.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/rays.h"
#include "terraingen/random/value_distribution.h"
#include "terraingen/util/geometry.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// The largest secondary crater's diameter at `range` meters from a primary of `diameter` meters,
/// in meters, after the generalized fit of Singer et al. (2020): d = a R^-b with a = 0.022 D^1.96
/// and b = 0.14 ln D, D and R in km.
double SingerMaxSecondaryDiameter(double diameter, double range);

/// The range in meters beyond which `SingerMaxSecondaryDiameter` falls below `size` meters: its
/// inverse in range. Infinite for primaries of 1 km or less, whose largest secondary does not
/// shrink with range.
double SingerMaxRange(double diameter, double size);

/// How far from its center a primary of `diameter` meters throws its first secondaries (Singer et
/// al. 2020): the radius plus about 1.8 diameters up to 3 km and 1.3 from 31 km, in meters.
double SecondaryOnsetRange(double diameter);

/// The median ray length in meters of Elliott et al. (2018, eq. 2): `coefficient` R^`exponent` km
/// for a crater of radius R km.
/// @param diameter In meters.
/// @param coefficient In km.
double ElliottMedianRayLength(double diameter, double coefficient = 6.59, double exponent = 1.27);

/// Secondaries gathered into their primary's rays and clumped along them, as they commonly lie
/// in radial chains and clusters pointing back to the primary (Singer et al. 2020); off by
/// default. `share` of the secondaries follow the rays of `layout`, each ray's across-profile
/// Gaussian at the ray's width 2 radii from the center but never narrower than
/// `minHalfAngleDegrees`, and clumped along the ray by noise in log range with a wavelength of
/// `clusterSpacing`. The count is kept: secondaries move into the rays, none are added. The
/// share, floor angle, clustering and spacing are illustrative, as the sources do not measure
/// them. Give a crater albedo layer the same layout for bright rays over the secondaries.
struct SecondaryRays {
    bool enabled = false;              ///< Whether secondaries follow rays.
    RayLayout layout{};                ///< The rays.
    double share = 0.7;                ///< Share of secondaries in rays, in [0, 1].
    double minHalfAngleDegrees = 2.0;  ///< Narrowest half width of a ray, in (0, 90) degrees.
    double clustering = 0.8;           ///< How strongly they clump along a ray, in [0, 1].
    double clusterSpacing = 0.15;      ///< Wavelength of the clumps in ln range; positive.

    /// Memberwise equality.
    bool operator==(const SecondaryRays& other) const {
        return enabled == other.enabled && layout == other.layout && share == other.share &&
               minHalfAngleDegrees == other.minHalfAngleDegrees && clustering == other.clustering &&
               clusterSpacing == other.clusterSpacing;
    }
    bool operator!=(const SecondaryRays& other) const {
        return !(*this == other);
    }
};

/// Secondaries stretched toward their primary, elliptical along the line back to it and rounder
/// with range, as distant secondaries are (Singer et al. 2020); round by default. The ellipticity
/// falls linearly to 1 at the outer range, which is illustrative.
struct SecondaryShapes {
    double elongatedShare = 0.0;  ///< Share of secondaries that are elliptical, in [0, 1].
    /// Long over short axis near the primary; at least 1 and bounded.
    ValueDistribution ellipticity{ValueDistribution::Kind::Uniform, 1.2, 1.8};

    /// Memberwise equality.
    bool operator==(const SecondaryShapes& other) const {
        return elongatedShare == other.elongatedShare && ellipticity == other.ellipticity;
    }
    bool operator!=(const SecondaryShapes& other) const {
        return !(*this == other);
    }
};

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
    /// Outer range in primary diameters from the center; unset uses 1.6 times Elliott et al.'s
    /// median ray length (`ElliottMedianRayLength`).
    std::optional<double> outerRange;
    /// The primaries that throw secondaries.
    CraterSelection selection{};
    /// Polar cell side as a multiple of each size band's largest diameter.
    double cellSizeFactor = 16.0;
    /// Most secondaries expected over one region, to catch requests that would exhaust memory.
    double maxExpected = 5e7;
    SecondaryRays rays{};      ///< Rays and clusters; off by default.
    SecondaryShapes shapes{};  ///< Elongation toward the primary; round by default.
    /// Source cells across from which the source's measured data already holds a secondary: with
    /// a source given to the layer, secondaries at least that many of its cells across where they
    /// land are left out (`SourceResolvedLimit`). Unset, or without a source, keeps every one.
    std::optional<double> sourceResolvesPixels = 4.0;

    /// Memberwise equality.
    bool operator==(const SecondaryCraterOptions& other) const {
        return seed == other.seed && onsetCount == other.onsetCount &&
               countFraction == other.countFraction && sizeSlope == other.sizeSlope &&
               minDiameter == other.minDiameter && maxDiameter == other.maxDiameter &&
               outerRange == other.outerRange && selection == other.selection &&
               cellSizeFactor == other.cellSizeFactor && maxExpected == other.maxExpected &&
               rays == other.rays && shapes == other.shapes &&
               sourceResolvesPixels == other.sourceResolvesPixels;
    }
    bool operator!=(const SecondaryCraterOptions& other) const {
        return !(*this == other);
    }
};

/// Checks secondary crater options.
/// @throws std::invalid_argument without a positive `onsetCount`, for a non-positive count
/// fraction or size slope, an empty or negative diameter range, a non-positive outer range, an
/// invalid selection, a non-positive cell size factor or limit, rays with an invalid layout, a
/// share or clustering outside [0, 1], a floor angle outside (0, 90) or a non-positive spacing, or
/// shapes with a share outside [0, 1] or an ellipticity that can be below 1 or unbounded.
void ValidateSecondaryCraterOptions(const SecondaryCraterOptions& options);

/// The outer range from its center, in meters, of the secondaries of a primary of `diameter`
/// meters.
double SecondaryOuterRange(double diameter, const SecondaryCraterOptions& options);

/// The largest secondary a primary of `diameter` meters throws under `options`, in meters: the
/// larger of `SingerMaxSecondaryDiameter` at the onset and the outer range, at most `maxDiameter`;
/// 0 when its outer range does not pass its onset.
double LargestSecondary(double diameter, const SecondaryCraterOptions& options);

/// Where a primary's secondaries gather: its rays and the clumps along them, as a density over
/// the even spread's.
struct SecondaryRayField {
    std::vector<RayDirection> rays;
    double diameter = 0.0;    ///< The primary's, in meters.
    double onset = 0.0;       ///< `SecondaryOnsetRange`, in meters.
    double share = 0.0;       ///< `SecondaryRays::share`.
    double sinFloor = 0.0;    ///< Sine of `SecondaryRays::minHalfAngleDegrees`.
    double clustering = 0.0;  ///< `SecondaryRays::clustering`.
    double spacing = 1.0;     ///< `SecondaryRays::clusterSpacing`.
    double meanClump = 1.0;   ///< The mean clump bump, which the clumping is divided by.

    /// The rays of `primary`'s secondaries under `rays`; none when they are off or the primary
    /// has no rays.
    static std::optional<SecondaryRayField> Make(const Crater& primary, const SecondaryRays& rays);

    /// The across-ray half width of `ray` at `range` meters.
    double Width(const RayDirection& ray, double range) const;

    /// The across-ray profile of `ray` at `range` meters and `azimuth`, 1 on its center line.
    double Profile(const RayDirection& ray, double range, double azimuth) const;

    /// The clumping along `ray` at `range` meters, 1 on average.
    double Clump(const RayDirection& ray, double range) const;

    /// The mean over azimuth of the summed profiles at `range` meters.
    double MeanProfile(double range) const;

    /// The density of secondaries at `range` meters and `azimuth` over the even spread's.
    double Density(double range, double azimuth) const;

    /// An upper bound of `Density` between `r0` and `r1` meters, with a margin.
    double Peak(double r0, double r1) const;
};

/// The density of the secondaries one primary throws, on average, as `SecondaryCraterLayer` draws
/// them: rings of n(R) (d / (`countFraction` D))^-`sizeSlope` below the largest secondary at each
/// range, gathered into rays when they are on.
class SecondaryDensity {
  public:
    /// The secondaries `primary` throws under `options`; none when its selection leaves it out.
    SecondaryDensity(const Crater& primary, const SecondaryCraterOptions& options);

    /// Secondaries per m^2 with diameters in [`dMin`, `dMax`) meters at `offset` meters east and
    /// north of the primary's center, in its local frame.
    double At(const Vec2d& offset, double dMin, double dMax) const;

    /// The share of ground covered by those secondaries, their areas pi d^2 / 4 summed per m^2.
    double CoverAt(const Vec2d& offset, double dMin, double dMax) const;

    /// The farthest a secondary lands from the primary's center, in meters; 0 when it throws none.
    double Reach() const {
        return outer_;
    }

    /// The primary's diameter, in meters.
    double Diameter() const {
        return diameter_;
    }

    /// Where the secondaries start (`SecondaryOnsetRange`), in meters from the primary's center.
    double Onset() const {
        return onset_;
    }

    /// Secondaries per m^2 at least `Reference` across at the onset.
    double OnsetDensity() const {
        return density0_;
    }

    /// The size `OnsetDensity` counts from, `countFraction` times the diameter, in meters.
    double Reference() const {
        return reference_;
    }

    /// Where the secondaries gather; none spreads them evenly.
    const std::optional<SecondaryRayField>& Rays() const {
        return rays_;
    }

    /// Chapman's scaled range 1 + (range - onset) / D at `range` meters, whose inverse square the
    /// density falls with.
    double ScaledRange(double range) const {
        return 1.0 + (range - onset_) / diameter_;
    }

    /// The share of the cumulative count at `Reference` with diameters in [`low`, `high`) meters.
    double SizeShare(double low, double high) const;

  private:
    /// The count, or with `cover` the covered share, per m^2 in [`dMin`, `dMax`) at `offset`.
    double Measure(const Vec2d& offset, double dMin, double dMax, bool cover) const;

    double sizeSlope_ = 4.0;
    double minSize_ = 0.0;
    double maxSize_ = 0.0;
    double diameter_ = 0.0;
    double onset_ = 0.0;
    double outer_ = 0.0;
    double density0_ = 0.0;
    double reference_ = 0.0;
    std::optional<SecondaryRayField> rays_;
};

}  // namespace terraingen
