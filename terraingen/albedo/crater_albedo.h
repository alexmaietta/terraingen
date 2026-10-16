/// @file
/// @brief Bright, immature ejecta, interiors and rays around fresh craters.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/albedo/maturity.h"
#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/shaping/irregularity.h"
#include "terraingen/layers/fade.h"
#include "terraingen/random/value_distribution.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// @brief How long a crater keeps its immature, bright material, by diameter, after Werner and
/// Medvedev (2010): `smallMyr` up to `smallDiameter`, a power law of diameter up to `midDiameter`,
/// `midMyr` up to `largeDiameter` and `largeMyr` beyond, every time multiplied by `scale`.
struct RayRetention {
    double smallDiameter = 1000.0;   ///< At or below this diameter (m), `smallMyr`.
    double smallMyr = 5.0;           ///< Retention of small craters, Myr.
    double midDiameter = 5000.0;     ///< From this diameter (m) to `largeDiameter`, `midMyr`.
    double midMyr = 750.0;           ///< Retention of mid-sized craters, Myr.
    double largeDiameter = 50000.0;  ///< Above this diameter (m), `largeMyr`.
    double largeMyr = 750.0;         ///< Retention of large craters, Myr.
    double scale = 1.0;              ///< Multiplies every time.

    /// @brief The retention time of a crater.
    /// @param diameter The crater's diameter, in metres.
    /// @return The time, in Myr.
    double Myr(double diameter) const;

    bool operator==(const RayRetention&) const = default;
};

/// @brief Checks ray retention settings.
/// @param retention The settings to check.
/// @throws std::invalid_argument unless the diameters are positive and increasing and the times
/// and scale positive, all finite.
void ValidateRayRetention(const RayRetention& retention);

/// @brief How oblique impacts shape a crater's rays. Each crater gets an impact angle from the
/// horizontal with P(< theta) = sin^2 theta and a downrange direction, `heading` or uniform at
/// random.
struct ObliqueImpacts {
    bool enabled = true;              ///< Whether impacts can be oblique; otherwise all vertical.
    std::optional<double> heading;    ///< Downrange, radians counter-clockwise from east.
    double asymmetricDegrees = 45.0;  ///< Below this angle rays are longer downrange.
    double forbiddenDegrees = 25.0;   ///< Below this angle rays near uprange are dropped.
    double butterflyDegrees = 10.0;   ///< Below this angle rays near downrange are dropped too.
    /// Downrange over uprange ejection speed, at least 1.
    ValueDistribution downrangeSpeedRatio{ValueDistribution::Kind::Uniform, 1.2, 1.4};
    double zoneHalfWidthDegrees = 45.0;  ///< Half width of a forbidden zone, in [0, 180].

    /// @brief The most a ray's length can be multiplied by.
    /// @return (1 + a)^2 / (1 + a^2 / 2) for the highest speed ratio's asymmetry a; 1 when
    /// disabled.
    double MaxStretch() const;

    bool operator==(const ObliqueImpacts&) const = default;
};

/// @brief The impact that formed a crater, as `ObliqueImpacts` draws it.
struct CraterImpact {
    double angleDegrees = 90.0;  ///< From the horizontal.
    double heading = 0.0;        ///< Downrange, radians counter-clockwise from east.
    double speedRatio = 1.0;     ///< Downrange over uprange ejection speed.
};

/// @brief Checks oblique impact settings.
/// @param oblique The settings to check.
/// @throws std::invalid_argument for an invalid speed ratio distribution or one that can be below
/// 1, angles outside [0, 90] or out of order (butterfly <= forbidden <= asymmetric), a zone half
/// width outside [0, 180], or a heading that is not finite.
void ValidateObliqueImpacts(const ObliqueImpacts& oblique);

/// @brief The rays of a crater: narrow streaks of immature material running out from the rim,
/// after Elliott et al. (2018), at uniform azimuths, fading linearly to their ends and broken by
/// value noise.
struct CraterRays {
    bool enabled = true;  ///< Whether craters have rays.
    /// Rays per crater, 0 to 64.
    ValueDistribution count{ValueDistribution::Kind::Normal, 10.7, 3.1, 6.0, 20.0};
    double lengthCoefficient = 6.59;  ///< Coefficient of `ElliottMedianRayLength`.
    double lengthExponent = 1.27;     ///< Exponent of `ElliottMedianRayLength`.
    /// Natural log of a ray's length over the median.
    ValueDistribution lengthSpread{ValueDistribution::Kind::Uniform, -0.356, 0.468};
    /// Angular width 2 radii from the centre, in (0, 180) degrees; the width there holds beyond.
    ValueDistribution widthDegrees{ValueDistribution::Kind::Normal, 22.0, 8.3, 5.4, 38.6};
    double excess = 0.048;     ///< Maturity excess at the rim, in OMAT; >= 0.
    double patchiness = 0.5;   ///< Most of the excess the noise takes away, in [0, 1].
    double patchRadii = 0.5;   ///< Noise wavelength, in crater radii; positive.
    ObliqueImpacts oblique{};  ///< How oblique impacts shape the rays.

    /// @brief The median ray length of a crater.
    /// @param diameter The crater's diameter, in metres.
    /// @return The length from its centre, in metres.
    double MedianLength(double diameter) const;

    bool operator==(const CraterRays&) const = default;
};

/// @brief Checks ray settings.
/// @param rays The settings to check.
/// @throws std::invalid_argument for invalid distributions, a count that can be below 0 or above
/// 64, widths outside (0, 180) degrees, a non-positive coefficient or exponent, a negative excess,
/// a patchiness outside [0, 1] or a non-positive patch wavelength.
void ValidateCraterRays(const CraterRays& rays);

/// @brief The bright material of fresh craters, in optical maturity (OMAT) over the background's:
/// an interior excess, ejecta falling as a power of distance, and rays. Excesses fade with age
/// over the retention time, add over overlapping craters and are capped; the albedo then moves
/// with maturity (`ReflectanceAtMaturity`). The defaults are Tycho's.
struct CraterAlbedoOptions {
    std::uint32_t seed = 0;  ///< Seed of the per-crater draws.
    /// The craters with bright material at all.
    CraterSelection selection{};
    double share = 1.0;  ///< Share of selected craters with bright material.

    double backgroundMaturity = 0.202;  ///< OMAT of the background; positive.
    double maxMaturity = 0.42;          ///< Cap on OMAT; >= `backgroundMaturity`.
    double interiorExcess = 0.132;      ///< Excess inside the rim.
    double ejectaExcess = 0.076;        ///< Excess of the ejecta at the rim.
    double ejectaExponent = 3.0;        ///< The ejecta excess falls as (r / R)^-this.
    double ejectaReachRadii = 3.0;      ///< Where the ejecta excess reaches 0, in radii; > 1.
    CraterRays rays{};                  ///< The rays.
    RayRetention retention{};           ///< Retention time T by diameter.
    double fadeExponent = 1.0;          ///< Excesses fade as max(0, 1 - t / T)^this.
    /// Reflectance of the optical maturity origin (`ReflectanceAtMaturity`).
    double originReflectance = OpticalMaturityOrigin{}.reflectance;
    ResolutionFade fade{};  ///< How craters too small to resolve fade out.

    bool operator==(const CraterAlbedoOptions&) const = default;
};

/// @brief Checks crater albedo options.
/// @param options The options to check.
/// @throws std::invalid_argument for invalid rays, retention, fade or selection, a share outside
/// [0, 1], a background maturity that is not positive or a cap below it, negative excesses, a
/// non-positive ejecta exponent, a reach of 1 radius or less, or a non-positive fade exponent.
void ValidateCraterAlbedo(const CraterAlbedoOptions& options);

/// @brief The bright ejecta, interiors and rays of the craters of a `CraterLayer`, as
/// `CraterAlbedoOptions` describes, shaped by the craters' irregular outlines where the layer has
/// them. The craters come from the layer for the region being evaluated, widened by the longest
/// ray, so every grid, tile and point agrees; excesses are summed in crater ID order.
class CraterAlbedo final : public AlbedoLayer {
  public:
    /// @brief Builds the layer.
    /// @param craters The crater layer whose craters are bright.
    /// @param options How bright, and for how long.
    /// @throws std::invalid_argument for a null layer or invalid options.
    CraterAlbedo(std::shared_ptr<const CraterLayer> craters, CraterAlbedoOptions options = {});

    /// @brief Brightens the albedo at one point by the craters' summed excess.
    /// @param value The albedo there, changed in place; NaN stays NaN.
    /// @param bands Bands of `value` in use.
    /// @param x World x.
    /// @param y World y.
    /// @param context How the albedo is evaluated.
    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const AlbedoContext& context) const override;

    /// @brief `ApplyAt` at every cell centre, gathering craters once per block of rows.
    /// @param bands One raster per band on one grid, changed in place.
    /// @param context How the albedo is evaluated.
    void Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const override;

    /// @brief The excess optical maturity a crater gives a point.
    /// @param crater The crater.
    /// @param x Surface x of the point.
    /// @param y Surface y of the point.
    /// @param spacing The sample spacing, in metres.
    /// @return The excess, in OMAT.
    double Excess(const Crater& crater, double x, double y, double spacing) const;

    /// @brief The impact a crater came from, drawn from its ID.
    /// @param crater The crater.
    /// @return The impact; vertical without oblique impacts.
    CraterImpact Impact(const Crater& crater) const;

    /// @brief How far from its centre a crater can brighten the ground.
    /// @param crater The crater.
    /// @return The farthest of its ejecta and its rays, in metres; 0 without bright material.
    double Reach(const Crater& crater) const;

    /// @brief The options.
    /// @return How bright, and for how long.
    const CraterAlbedoOptions& Options() const {
        return options_;
    }

  private:
    struct Ray;
    struct Prepared;
    Prepared Prepare(const Crater& crater, double spacing) const;
    /// Whether `crater` is selected and drawn to have bright material, from the next uniform of
    /// `random`.
    bool Eligible(const Crater& crater, RandomStream& random) const;
    /// The next ray of `random`, for a crater of median ray length `median` formed by `impact`.
    Ray DrawRay(RandomStream& random, double median, const CraterImpact& impact) const;
    /// Draws the rays of `prepared`, widening its reach to the longest.
    void AddRays(Prepared& prepared, RandomStream& random) const;
    std::vector<Prepared> Gather(const Bounds2d& region, double spacing) const;

    std::shared_ptr<const CraterLayer> craters_;
    CraterAlbedoOptions options_;
};

}  // namespace terraingen
