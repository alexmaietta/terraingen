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

/// How long a crater keeps its immature, bright material, by diameter, after Werner and
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

    /// The retention time in Myr of a crater `diameter` metres across.
    double Myr(double diameter) const;

    bool operator==(const RayRetention&) const = default;
};

/// Checks ray retention settings.
/// @throws std::invalid_argument unless the diameters are positive and increasing and the times
/// and scale positive, all finite.
void ValidateRayRetention(const RayRetention& retention);

/// How oblique impacts shape a crater's rays. Each crater gets an impact angle from the
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

    /// The most a ray's length can be multiplied by: (1 + a)^2 / (1 + a^2 / 2) for the highest
    /// speed ratio's asymmetry a; 1 when disabled.
    double MaxStretch() const;

    bool operator==(const ObliqueImpacts&) const = default;
};

/// The impact that formed a crater, as `ObliqueImpacts` draws it.
struct CraterImpact {
    double angleDegrees = 90.0;  ///< From the horizontal.
    double heading = 0.0;        ///< Downrange, radians counter-clockwise from east.
    double speedRatio = 1.0;     ///< Downrange over uprange ejection speed.
};

/// Checks oblique impact settings.
/// @throws std::invalid_argument for an invalid speed ratio distribution or one that can be below
/// 1, angles outside [0, 90] or out of order (butterfly <= forbidden <= asymmetric), a zone half
/// width outside [0, 180], or a heading that is not finite.
void ValidateObliqueImpacts(const ObliqueImpacts& oblique);

/// The rays of a crater: narrow streaks of immature material running out from the rim,
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

    /// The median ray length from the centre, in metres, of a crater `diameter` metres across.
    double MedianLength(double diameter) const;

    bool operator==(const CraterRays&) const = default;
};

/// Checks ray settings.
/// @throws std::invalid_argument for invalid distributions, a count that can be below 0 or above
/// 64, widths outside (0, 180) degrees, a non-positive coefficient or exponent, a negative excess,
/// a patchiness outside [0, 1] or a non-positive patch wavelength.
void ValidateCraterRays(const CraterRays& rays);

/// The texture of fresh craters' bright material, keyed to each crater and off by default: radial
/// streaks breaking ejecta, rays and interiors into filaments, a lobed ejecta edge, and walls
/// brighter than floors.
struct CraterAlbedoTexture {
    /// Share of the excess the radial streaks take away between filaments, in [0, 1]; 0 for none.
    double streaks = 0.0;
    double streakWidthDegrees = 3.0;  ///< Typical width of a filament around the crater; positive.
    double streakLengthRadii = 1.5;   ///< Typical length of a filament outward, in radii; positive.
    /// How far beyond the rim the ejecta stay continuous before breaking into filaments, in radii;
    /// the streaks ramp in over that distance, and 0 streaks them from the rim.
    double continuousRadii = 0.0;
    /// How far the ejecta's edge wanders in and out, as a share of its reach, in [0, 0.9]; 0 keeps
    /// it circular.
    double lobes = 0.0;
    int lobeCount = 8;        ///< Typical number of lobes around a crater, 1 to 64.
    double floorShare = 1.0;  ///< Interior excess on the floor over that at the rim, in [0, 1].
    std::uint32_t seed = 0;   ///< Varies every crater's texture.

    bool operator==(const CraterAlbedoTexture&) const = default;
};

/// Checks crater albedo texture settings.
/// @throws std::invalid_argument for streaks or a floor share outside [0, 1], lobes outside
/// [0, 0.9], a lobe count outside 1 to 64, a non-positive streak width or length, or a
/// negative continuous reach.
void ValidateCraterAlbedoTexture(const CraterAlbedoTexture& texture);

/// The bright material of fresh craters, in optical maturity (OMAT) over the background's:
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
    CraterAlbedoTexture texture{};      ///< Filaments, lobes and wall brightness; off by default.
    RayRetention retention{};           ///< Retention time T by diameter.
    double fadeExponent = 1.0;          ///< Excesses fade as max(0, 1 - t / T)^this.
    /// Reflectance of the optical maturity origin (`ReflectanceAtMaturity`).
    double originReflectance = OpticalMaturityOrigin{}.reflectance;
    ResolutionFade fade{};  ///< How craters too small to resolve fade out.

    bool operator==(const CraterAlbedoOptions&) const = default;
};

/// Checks crater albedo options.
/// @throws std::invalid_argument for invalid rays, retention, fade or selection, a share outside
/// [0, 1], a background maturity that is not positive or a cap below it, negative excesses, a
/// non-positive ejecta exponent, a reach of 1 radius or less, or a non-positive fade exponent.
void ValidateCraterAlbedo(const CraterAlbedoOptions& options);

/// The bright ejecta, interiors and rays of the craters of a `CraterLayer`, as
/// `CraterAlbedoOptions` describes, shaped by the craters' irregular outlines where the layer has
/// them. The craters come from the layer for the region being evaluated, widened by the longest
/// ray, so every grid, tile and point agrees; excesses are summed in crater ID order.
class CraterAlbedo final : public AlbedoLayer {
  public:
    /// The bright material of the craters of `craters`.
    /// @throws std::invalid_argument for a null layer or options invalid as `ValidateCraterAlbedo`.
    CraterAlbedo(std::shared_ptr<const CraterLayer> craters, CraterAlbedoOptions options = {});

    /// Brightens the albedo at one point by the craters' summed excess; NaN stays NaN.
    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const AlbedoContext& context) const override;

    /// `ApplyAt` at every cell centre, gathering craters once per block of rows.
    void Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const override;

    /// The excess optical maturity (OMAT) `crater` gives surface point (`x`, `y`) at a sample
    /// spacing of `spacing` metres.
    double Excess(const Crater& crater, double x, double y, double spacing) const;

    /// The impact `crater` came from, drawn from its ID; vertical without oblique impacts.
    CraterImpact Impact(const Crater& crater) const;

    /// How far from its centre `crater` can brighten the ground: the farthest of its ejecta and
    /// its rays, in metres; 0 without bright material.
    double Reach(const Crater& crater) const;

    /// How bright, and for how long.
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
