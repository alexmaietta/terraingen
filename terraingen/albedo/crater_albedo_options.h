// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The options of `CraterAlbedo`: retention, rays, oblique impacts, texture and the
/// reflectance zones of new craters.
#pragma once

#include "terraingen/albedo/maturity.h"
#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/rays.h"
#include "terraingen/layers/fade.h"
#include "terraingen/random/value_distribution.h"

#include <array>
#include <cstdint>
#include <optional>

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

    /// The retention time in Myr of a crater `diameter` meters across.
    double Myr(double diameter) const;

    bool operator==(const RayRetention& other) const {
        return smallDiameter == other.smallDiameter && smallMyr == other.smallMyr &&
               midDiameter == other.midDiameter && midMyr == other.midMyr &&
               largeDiameter == other.largeDiameter && largeMyr == other.largeMyr &&
               scale == other.scale;
    }
    bool operator!=(const RayRetention& other) const {
        return !(*this == other);
    }
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

    bool operator==(const ObliqueImpacts& other) const {
        return enabled == other.enabled && heading == other.heading &&
               asymmetricDegrees == other.asymmetricDegrees &&
               forbiddenDegrees == other.forbiddenDegrees &&
               butterflyDegrees == other.butterflyDegrees &&
               downrangeSpeedRatio == other.downrangeSpeedRatio &&
               zoneHalfWidthDegrees == other.zoneHalfWidthDegrees;
    }
    bool operator!=(const ObliqueImpacts& other) const {
        return !(*this == other);
    }
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
    /// Angular width 2 radii from the center, in (0, 180) degrees; the width there holds beyond.
    ValueDistribution widthDegrees{ValueDistribution::Kind::Normal, 22.0, 8.3, 5.4, 38.6};
    double excess = 0.048;     ///< Maturity excess at the rim, in OMAT; >= 0.
    double patchiness = 0.5;   ///< Most of the excess the noise takes away, in [0, 1].
    double patchRadii = 0.5;   ///< Noise wavelength, in crater radii; positive.
    ObliqueImpacts oblique{};  ///< How oblique impacts shape the rays.
    /// Takes the rays' number, directions and widths from this layout (`RayDirections`) in place
    /// of `count` and `widthDegrees`, so they lie over secondaries with the same layout; unset
    /// draws them per crater as before.
    std::optional<RayLayout> layout;

    /// The median ray length from the center, in meters, of a crater `diameter` meters across.
    double MedianLength(double diameter) const;

    bool operator==(const CraterRays& other) const {
        return enabled == other.enabled && count == other.count &&
               lengthCoefficient == other.lengthCoefficient &&
               lengthExponent == other.lengthExponent && lengthSpread == other.lengthSpread &&
               widthDegrees == other.widthDegrees && excess == other.excess &&
               patchiness == other.patchiness && patchRadii == other.patchRadii &&
               oblique == other.oblique && layout == other.layout;
    }
    bool operator!=(const CraterRays& other) const {
        return !(*this == other);
    }
};

/// Checks ray settings.
/// @throws std::invalid_argument for invalid distributions, a count that can be below 0 or above
/// 64, widths outside (0, 180) degrees, a non-positive coefficient or exponent, a negative excess,
/// a patchiness outside [0, 1], a non-positive patch wavelength or an invalid layout.
void ValidateCraterRays(const CraterRays& rays);

/// The texture of fresh craters' bright material, keyed to each crater and off by default: radial
/// streaks breaking ejecta, rays, interiors and the bright proximal reflectance zone into
/// filaments, a lobed ejecta edge, and walls brighter than floors.
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

    bool operator==(const CraterAlbedoTexture& other) const {
        return streaks == other.streaks && streakWidthDegrees == other.streakWidthDegrees &&
               streakLengthRadii == other.streakLengthRadii &&
               continuousRadii == other.continuousRadii && lobes == other.lobes &&
               lobeCount == other.lobeCount && floorShare == other.floorShare && seed == other.seed;
    }
    bool operator!=(const CraterAlbedoTexture& other) const {
        return !(*this == other);
    }
};

/// Checks crater albedo texture settings.
/// @throws std::invalid_argument for streaks or a floor share outside [0, 1], lobes outside
/// [0, 0.9], a lobe count outside 1 to 64, a non-positive streak width or length, or a
/// negative continuous reach.
void ValidateCraterAlbedoTexture(const CraterAlbedoTexture& texture);

/// One reflectance zone around new small craters: a share of craters has it, it changes the
/// reflectance relative to the pre-impact surface by `change`, and it reaches
/// `coefficient` D^`exponent` meters from the center times `scale`, both drawn per crater, with
/// a ragged edge of fingers pulled in by up to `raggedness` of the reach, lasting `retentionMyr`.
struct ReflectanceZone {
    double share = 1.0;                ///< Share of craters with the zone, in [0, 1].
    ValueDistribution change{};        ///< Relative reflectance change, above -1.
    double coefficient = 1.0;          ///< Reach law coefficient, m; positive.
    double exponent = 1.0;             ///< Reach law exponent; positive.
    ValueDistribution scale{};         ///< Reach multiplier; positive and bounded.
    double raggedness = 0.0;           ///< Reach fingers pull in, in [0, 1).
    double fingerWidthDegrees = 10.0;  ///< Typical finger width; positive.
    double feather = 0.3;              ///< Share of the reach fading out, in (0, 1].
    /// How long the zone lasts (Myr), fading like the bright material; unset lasts as long as the
    /// bright material, and a zone outlasting it ends with it.
    std::optional<double> retentionMyr;

    bool operator==(const ReflectanceZone& other) const {
        return share == other.share && change == other.change && coefficient == other.coefficient &&
               exponent == other.exponent && scale == other.scale &&
               raggedness == other.raggedness && fingerWidthDegrees == other.fingerWidthDegrees &&
               feather == other.feather && retentionMyr == other.retentionMyr;
    }
    bool operator!=(const ReflectanceZone& other) const {
        return !(*this == other);
    }
};

/// Checks a reflectance zone.
/// @throws std::invalid_argument for a share outside [0, 1], an invalid change that can reach -1,
/// a non-positive coefficient or exponent, a scale that can be non-positive or unbounded, a
/// raggedness outside [0, 1), a non-positive finger width, a feather outside (0, 1] or a
/// non-positive retention time.
void ValidateReflectanceZone(const ReflectanceZone& zone);

/// Topographic shadows in the distal reflectance zones (Speyerer et al. 2016): ground below a jet
/// from the rim that drops no faster than `jetAngleDegrees` fades the zones; illustrative values.
struct ZoneShadows {
    bool enabled = false;          ///< Whether the distal zones have shadows; needs the ground.
    double jetAngleDegrees = 3.0;  ///< Steepest descent of the jet, in (0, 90) degrees.
    double depth = 0.5;            ///< Depth below the jet of full shadow (m); positive.
    int azimuths = 720;            ///< Rays around a crater, 8 to 8192.
    int radialSamples = 400;       ///< Samples along a ray, 8 to 8192.

    bool operator==(const ZoneShadows& other) const {
        return enabled == other.enabled && jetAngleDegrees == other.jetAngleDegrees &&
               depth == other.depth && azimuths == other.azimuths &&
               radialSamples == other.radialSamples;
    }
    bool operator!=(const ZoneShadows& other) const {
        return !(*this == other);
    }
};

/// The four reflectance zones around new small craters in LROC temporal pairs (Robinson et al.
/// 2015; Speyerer et al. 2016), off by default: proximal high and low, distal high and low, inner
/// zones covering outer ones.
struct CraterReflectanceZones {
    bool enabled = false;          ///< Whether craters get the zones.
    double maxDiameter = 50.0;     ///< Largest crater with zones (m); the fits stop near 43 m.
    double matureDepth = 1.0;      ///< Depth of mature regolith (m); not negative.
    double excavationRatio = 0.1;  ///< Excavation depth over diameter; positive.
    /// PHRZ: +12% +/- 9% to a median 1.5 D (mean 2.1 D) from the center.
    ReflectanceZone proximalHigh{1.0,
                                 {ValueDistribution::Kind::Normal, 0.12, 0.09, 0.01, 0.5},
                                 1.5,
                                 1.0,
                                 {ValueDistribution::Kind::LogNormal, 1.0, 0.82, 0.4, 5.0},
                                 0.3,
                                 25.0,
                                 0.3,
                                 {}};
    /// PLRZ: -9% +/- 4%, in nearly all craters, to a median 2.9 D (mean 3.2 D); lasting as long as
    /// the top centimeters is an inference from Robinson et al.'s (2015) discussion.
    ReflectanceZone proximalLow{1.0,
                                {ValueDistribution::Kind::Normal, -0.09, 0.04, -0.2, -0.01},
                                2.9,
                                1.0,
                                {ValueDistribution::Kind::LogNormal, 1.0, 0.44, 0.5, 3.0},
                                0.3,
                                20.0,
                                0.3,
                                0.081};
    /// DHRZ: +1% to +4% in 24% of craters, to 0.52 D^2.5 m.
    ReflectanceZone distalHigh{0.24,
                               {ValueDistribution::Kind::Uniform, 0.01, 0.04},
                               0.52,
                               2.5,
                               {ValueDistribution::Kind::Constant, 1.0},
                               0.5,
                               8.0,
                               0.4,
                               0.081};
    /// DLRZ: -2% to -6% in 84% of craters, ray-like, to 5.03 D^2 m.
    ReflectanceZone distalLow{0.84,
                              {ValueDistribution::Kind::Uniform, -0.06, -0.02},
                              5.03,
                              2.0,
                              {ValueDistribution::Kind::Constant, 1.0},
                              0.6,
                              8.0,
                              0.6,
                              0.081};
    ZoneShadows shadows{};   ///< Shadows of rises in the distal zones; off by default.
    std::uint32_t seed = 0;  ///< Varies every crater's zones.

    /// The farthest any zone reaches from the center of a crater `diameter` meters across.
    double MaxReach(double diameter) const;

    /// The four zones from the innermost: proximal high and low, then distal high and low.
    std::array<const ReflectanceZone*, 4> All() const {
        return {&proximalHigh, &proximalLow, &distalHigh, &distalLow};
    }

    bool operator==(const CraterReflectanceZones& other) const {
        return enabled == other.enabled && maxDiameter == other.maxDiameter &&
               matureDepth == other.matureDepth && excavationRatio == other.excavationRatio &&
               proximalHigh == other.proximalHigh && proximalLow == other.proximalLow &&
               distalHigh == other.distalHigh && distalLow == other.distalLow &&
               shadows == other.shadows && seed == other.seed;
    }
    bool operator!=(const CraterReflectanceZones& other) const {
        return !(*this == other);
    }
};

/// Checks crater reflectance zones.
/// @throws std::invalid_argument for an invalid zone, a non-positive largest diameter or
/// excavation ratio, a negative mature depth, or shadows with a jet angle outside (0, 90), a
/// non-positive depth, or sample counts outside 8 to 8192.
void ValidateCraterReflectanceZones(const CraterReflectanceZones& zones);

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
    CraterReflectanceZones zones{};     ///< Zones around new small craters; off by default.
    RayRetention retention{};           ///< Retention time T by diameter.
    double fadeExponent = 1.0;          ///< Excesses fade as max(0, 1 - t / T)^this.
    /// Reflectance of the optical maturity origin (`ReflectanceAtMaturity`).
    double originReflectance = kOpticalMaturityOriginReflectance;
    ResolutionFade fade{};  ///< How craters too small to resolve fade out.

    bool operator==(const CraterAlbedoOptions& other) const {
        return seed == other.seed && selection == other.selection && share == other.share &&
               backgroundMaturity == other.backgroundMaturity && maxMaturity == other.maxMaturity &&
               interiorExcess == other.interiorExcess && ejectaExcess == other.ejectaExcess &&
               ejectaExponent == other.ejectaExponent &&
               ejectaReachRadii == other.ejectaReachRadii && rays == other.rays &&
               texture == other.texture && zones == other.zones && retention == other.retention &&
               fadeExponent == other.fadeExponent && originReflectance == other.originReflectance &&
               fade == other.fade;
    }
    bool operator!=(const CraterAlbedoOptions& other) const {
        return !(*this == other);
    }
};

/// Checks crater albedo options.
/// @throws std::invalid_argument for invalid rays, retention, fade or selection, a share outside
/// [0, 1], a background maturity that is not positive or a cap below it, negative excesses, a
/// non-positive ejecta exponent, a reach of 1 radius or less, or a non-positive fade exponent.
void ValidateCraterAlbedoOptions(const CraterAlbedoOptions& options);

}  // namespace terraingen
