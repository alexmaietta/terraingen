#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/albedo/maturity.h"
#include "terraingen/layers/craters/apply.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/irregularity.h"

#include <cstdint>
#include <limits>
#include <memory>

namespace terraingen {

/// How long a crater keeps the immature, bright material that makes its ejecta and rays stand out,
/// by diameter, after Werner and Medvedev (2010): rayed craters of 5 to 50 km follow a 750 Myr
/// isochrone, and those under about 1 km a 5 Myr one, with retention falling gradually between
/// 1 and 5 km (interpolated here as a power law of diameter between the two). Their rayed craters
/// over 50 km lie between the 750 Myr and 2 Gyr isochrones, compositional rays lasting longer;
/// `largeMyr` keeps 750 Myr for them by default. Every time is multiplied by `scale`.
///
/// Reference: S. C. Werner and S. Medvedev, "The Lunar rayed-crater population - Characteristics
/// of the spatial distribution and ray retention", EPSL 295:147-158, 2010.
/// https://doi.org/10.1016/j.epsl.2010.03.036
struct RayRetention {
    double smallDiameter = 1000.0;  ///< At or below this diameter (m), `smallMyr`.
    double smallMyr = 5.0;
    double midDiameter = 5000.0;  ///< From this diameter (m) to `largeDiameter`, `midMyr`.
    double midMyr = 750.0;
    double largeDiameter = 50000.0;  ///< Above this diameter (m), `largeMyr`.
    double largeMyr = 750.0;
    double scale = 1.0;

    /// The retention time in Myr of a crater `diameter` across.
    double Myr(double diameter) const;

    bool operator==(const RayRetention&) const = default;
};

/// @throws std::invalid_argument unless the diameters are positive and increasing and the times
/// and scale positive, all finite.
void ValidateRayRetention(const RayRetention& retention);

/// The rays of a crater: narrow streaks of immature material and secondary craters running out
/// from the rim, after Elliott et al. (2018), who mapped the rays of 26 lunar craters of 10 m to
/// 84 km diameter (their Table 2).
///
/// - Count: they average 10.7 rays (6 to 20); the default is normal with that mean and the
///   standard deviation of their Table 2 counts, 3.1, clamped to 6 to 20 and rounded.
/// - Length: the median ray of a crater of radius R km is `lengthCoefficient` R^`lengthExponent`
///   km long (their eq. 2: 6.59 R^1.27, fitted to craters over about 100 m radius). Each ray's
///   length is the median times e to `lengthSpread`: by default uniform between the means, over
///   the 15 craters of Table 2 they fitted, of ln(min / median) and ln(max / median), -0.356 and
///   0.468.
/// - Width: the polar angle a ray spans 2 radii from the centre, where continuous ejecta breaks
///   into rays, averages 22 degrees with a standard deviation of 8.3; the default is normal with
///   those, clamped to 2 standard deviations either side. A ray keeps that angle inside 2 radii and
///   the width it has there beyond them, with a cross-section falling as (1 - (d / h)^2)^2 to its
///   edge at half-width h.
/// - Strength: an excess of optical maturity over the background of `excess` at the rim, falling
///   linearly to 0 at the ray's end. The default, 0.048, is Elliott et al.'s OMAT of Tycho's rays
///   4 radii out (0.25) over Lucey et al.'s (2000) Tycho background (0.202).
/// - Breaks: rays are chains of secondary clusters with gaps; value noise along the ray, of
///   wavelength `patchRadii` radii, takes up to `patchiness` of the strength away. Both defaults
///   are illustrative, not measured.
///
/// Azimuths are uniform at random. Elliott et al. note oblique impacts lengthen rays downrange,
/// which is not modelled.
///
/// Reference: J. R. Elliott, Y.-H. Huang, D. A. Minton and A. M. Freed, "The length of lunar crater
/// rays explained using secondary crater scaling", Icarus 312:231-246, 2018.
/// https://doi.org/10.1016/j.icarus.2018.04.015
struct CraterRays {
    bool enabled = true;
    ValueDistribution count{ValueDistribution::Kind::Normal, 10.7, 3.1, 6.0, 20.0};
    double lengthCoefficient = 6.59;
    double lengthExponent = 1.27;
    ValueDistribution lengthSpread{ValueDistribution::Kind::Uniform, -0.356, 0.468};
    ValueDistribution widthDegrees{ValueDistribution::Kind::Normal, 22.0, 8.3, 5.4, 38.6};
    double excess = 0.048;
    double patchiness = 0.5;
    double patchRadii = 0.5;

    /// The median ray length (m) of a crater `diameter` across.
    double MedianLength(double diameter) const;

    bool operator==(const CraterRays&) const = default;
};

/// @throws std::invalid_argument for invalid distributions, a count that can be below 0 or above
/// 64, widths outside (0, 180) degrees, a non-positive coefficient or exponent, a negative excess,
/// a patchiness outside [0, 1] or a non-positive patch wavelength.
void ValidateCraterRays(const CraterRays& rays);

/// Settings for a `CraterAlbedo`.
///
/// A fresh crater's material is less mature than the ground around it, so brighter. In optical
/// maturity (OMAT, `OpticalMaturity`) units over the background's `backgroundMaturity`:
/// - inside the rim it is `interiorExcess` higher;
/// - outside it, on the continuous ejecta, `ejectaExcess` higher at the rim, falling with distance
///   r as the ejecta's thickness does, (r / R)^-`ejectaExponent` for crater radius R (McGetchin
///   et al. 1973: B = 3 for lunar craters), shifted to reach 0 at `ejectaReachRadii`;
/// - along its rays as `CraterRays` describes.
///
/// The defaults are Tycho's (Lucey et al. 2000, Table 4): interior 0.334, ejecta 0.278 and
/// background 0.202 OMAT, Tycho's ejecta value taken at the rim. Continuous ejecta reaches about a
/// radius, perhaps two, beyond the rim, with under 10% of it beyond 3 radii (Melosh 1989, as Grier
/// et al. 2001 summarize), hence a reach of 3 radii.
///
/// The excess fades with age t as max(0, 1 - t / T)^`fadeExponent`, with T the crater's retention
/// time (`RayRetention`). How immaturity fades with age is not measured in these sources; the
/// shape is configurable, a straight fade by default. Excesses of overlapping craters add, and the
/// total is capped at `maxMaturity`, the top of Grier et al.'s range for immature ejecta near
/// crater rims (0.29 to 0.42). The albedo then moves with maturity at fixed iron content
/// (`ReflectanceAtMaturity`).
///
/// References:
/// - P. G. Lucey, D. T. Blewett, G. J. Taylor and B. R. Hawke, "Imaging of lunar surface
///   maturity", JGR 105(E8):20377-20386, 2000. https://doi.org/10.1029/1999JE001110
/// - J. A. Grier, A. S. McEwen, P. G. Lucey, M. Milazzo and R. G. Strom, "Optical maturity of
///   ejecta from large rayed lunar craters", JGR 106(E12):32847-32862, 2001.
///   https://doi.org/10.1029/1999JE001160
/// - T. R. McGetchin, M. Settle and J. W. Head, "Radial thickness variation in impact crater
///   ejecta: implications for lunar basin deposits", EPSL 20:226-236, 1973.
///   https://doi.org/10.1016/0012-821X(73)90162-3
/// - B. R. Hawke et al., "The origin of lunar crater rays", Icarus 170:1-16, 2004: rays are bright
///   from immature material, from material of contrasting composition, or both; only immaturity is
///   modelled here. https://doi.org/10.1016/j.icarus.2004.02.013
struct CraterAlbedoOptions {
    std::uint32_t seed = 0;
    /// Craters with bright material at all: diameters (m) and ages (Gyr) within these, and manual
    /// craters only with `manual`.
    double minDiameter = 0.0;
    double maxDiameter = std::numeric_limits<double>::infinity();
    double minAgeGyr = 0.0;
    double maxAgeGyr = std::numeric_limits<double>::infinity();
    bool manual = true;
    double share = 1.0;  ///< Share of eligible craters with bright material.

    double backgroundMaturity = 0.202;
    double maxMaturity = 0.42;
    double interiorExcess = 0.132;
    double ejectaExcess = 0.076;
    double ejectaExponent = 3.0;
    double ejectaReachRadii = 3.0;
    CraterRays rays{};
    RayRetention retention{};
    double fadeExponent = 1.0;
    double originReflectance = OpticalMaturityOrigin{}.reflectance;
    ResolutionFade fade{};

    bool operator==(const CraterAlbedoOptions&) const = default;
};

/// @throws std::invalid_argument for invalid rays, retention or fade, inverted or negative size or
/// age ranges, a share outside [0, 1], a background maturity that is not positive or a cap below
/// it, negative excesses, a non-positive ejecta exponent, a reach of 1 radius or less, or a
/// non-positive fade exponent.
void ValidateCraterAlbedo(const CraterAlbedoOptions& options);

/// The bright ejecta, interiors and rays of the craters of a `CraterLayer`, as
/// `CraterAlbedoOptions` describes, shaped by the craters' irregular outlines where the layer has
/// them. The craters come from the layer for the region being evaluated, widened by the longest
/// ray, so every grid, tile and point agrees; excesses are summed in crater ID order.
class CraterAlbedo final : public AlbedoLayer {
  public:
    /// @throws std::invalid_argument for a null layer or invalid options.
    CraterAlbedo(std::shared_ptr<const CraterLayer> craters, CraterAlbedoOptions options = {});

    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const AlbedoContext& context) const override;

    void Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const override;

    /// The excess optical maturity `crater` gives (`x`, `y`) when sampled every `spacing`.
    double Excess(const Crater& crater, double x, double y, double spacing) const;

    /// How far from its centre `crater` can brighten the ground: the farthest of its ejecta and
    /// its rays; 0 when it has no bright material.
    double Reach(const Crater& crater) const;

    const CraterAlbedoOptions& Options() const {
        return options_;
    }

  private:
    struct Prepared;
    Prepared Prepare(const Crater& crater, double spacing) const;
    std::vector<Prepared> Gather(const Bounds2d& region, double spacing) const;

    std::shared_ptr<const CraterLayer> craters_;
    CraterAlbedoOptions options_;
};

}  // namespace terraingen
