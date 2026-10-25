// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Splotches: the dark and bright marks secondary impacts of loose regolith leave.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/fade.h"
#include "terraingen/random/stream.h"
#include "terraingen/random/value_distribution.h"
#include "terraingen/sources/terrain_base.h"
#include "terraingen/util/cells.h"
#include "terraingen/util/surface.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// Swarms of splotches thrown by new craters, the low-velocity secondaries of their ejecta
/// (Robinson et al. 2015; Speyerer et al. 2016; Housen and Holsapple 2011), off by default.
struct SplotchSwarms {
    bool enabled = false;             ///< Whether new craters throw swarms.
    double craterRate = 4.84e-6;      ///< New craters of at least 10 m, per km^2 per year.
    double craterSlope = 3.06;        ///< Cumulative slope of the new craters; positive.
    double minCraterDiameter = 10.0;  ///< Smallest crater with a swarm (m); positive.
    double maxCraterDiameter = 50.0;  ///< Largest (m); above the smallest.
    double referenceDiameter = 18.8;  ///< Diameter of the reference crater (m).
    double referenceCount = 248.0;    ///< Splotches of the reference crater; positive.
    double mu = 0.41;                 ///< Velocity exponent of the target, in (0, 1).
    double minRange = 4300.0;         ///< Nearest landing distance (m); positive.
    double maxRange = 30000.0;        ///< Farthest landing distance (m); above the nearest.
    /// Diameter of a swarm splotch (m); positive and bounded.
    ValueDistribution splotchDiameter{ValueDistribution::LogNormal(6.0, 0.6).Clamped(2.0, 22.0)};
    double darkShare = 239.0 / 248.0;  ///< Share darker than the ground, in [0, 1].
    double brightShare = 3.0 / 248.0;  ///< Share brighter; the rest are mixed.
    /// Reflectance change of a dark swarm splotch; above -1 and bounded.
    ValueDistribution darkChange{ValueDistribution::Constant(-0.04)};
    /// Reflectance change of a bright swarm splotch; above -1 and bounded.
    ValueDistribution brightChange{ValueDistribution::Uniform(0.04, 0.12)};
    double wedgeShare = 0.2;       ///< Share of wedges pointing back to the crater, [0, 1].
    double wedgeElongation = 2.0;  ///< Long over short axis of a wedge; at least 1.
    /// New craters given by hand, such as an observed impact, each throwing a swarm like the
    /// generated ones; IDs of 0 come from `ManualCraterId`.
    std::vector<Crater> craters;

    /// New craters of at least 1 m per m^2 within `lifetimeMyr`.
    double Coefficient(double lifetimeMyr) const;

    /// The splotches a crater `diameter` meters across throws, on average.
    double Count(double diameter) const;

    bool operator==(const SplotchSwarms& other) const {
        return enabled == other.enabled && craterRate == other.craterRate &&
               craterSlope == other.craterSlope && minCraterDiameter == other.minCraterDiameter &&
               maxCraterDiameter == other.maxCraterDiameter &&
               referenceDiameter == other.referenceDiameter &&
               referenceCount == other.referenceCount && mu == other.mu &&
               minRange == other.minRange && maxRange == other.maxRange &&
               splotchDiameter == other.splotchDiameter && darkShare == other.darkShare &&
               brightShare == other.brightShare && darkChange == other.darkChange &&
               brightChange == other.brightChange && wedgeShare == other.wedgeShare &&
               wedgeElongation == other.wedgeElongation && craters == other.craters;
    }
    bool operator!=(const SplotchSwarms& other) const {
        return !(*this == other);
    }
};

/// Splotches after Speyerer et al. (2016): formed at `rateCoefficient` d^-`rateExponent` per km^2
/// per year (d in km), each lasting `lifetimeMyr`; dark, bright or mixed.
struct SplotchAlbedoOptions {
    std::uint32_t seed = 0;              ///< Seed of where splotches fall and what they are.
    double rateCoefficient = 1.483e-11;  ///< Formation rate coefficient, per km^2 per year.
    double rateExponent = 4.144;         ///< Cumulative slope of the formation rate; positive.
    double lifetimeMyr = 0.081;          ///< How long a splotch lasts, fading linearly (Myr).
    double minDiameter = 1.0;    ///< Smallest splotch (m); the fit is extrapolated below 10.
    double maxDiameter = 40.0;   ///< Largest splotch (m), above `minDiameter`.
    double darkShare = 0.907;    ///< Share of dark splotches, in [0, 1].
    double brightShare = 0.074;  ///< Share of bright ones; the rest are mixed.
    /// Reflectance change of a dark splotch, relative to the ground before it; above -1.
    ValueDistribution darkChange{ValueDistribution::Constant(-0.04)};
    /// Reflectance change of a bright splotch; above -1.
    ValueDistribution brightChange{ValueDistribution::Constant(0.10)};
    /// Share of splotches on slopes steeper than `steepSlopeDegrees` that are bright, in [0, 1];
    /// unset keeps `brightShare`. Bright splotches are found mostly on such slopes, where mature
    /// regolith is thin (Speyerer et al. 2016), but the sources give no share; needs the ground.
    std::optional<double> steepBrightShare;
    double steepSlopeDegrees = 15.0;  ///< Slope from which ground is steep, in (0, 90) degrees.
    double softness = 0.3;            ///< Share of the radius over which the edge fades, in (0, 1].
    double raggedness = 0.15;         ///< Share of the radius the edge's lobes pull in, in [0, 1).
    ResolutionFade fade{};            ///< How splotches too small to resolve fade out.
    SplotchSwarms swarms{};           ///< Swarms around new craters; off by default.

    /// The cumulative number of splotches of diameter at least 1 m per m^2 at any time.
    double Coefficient() const;

    bool operator==(const SplotchAlbedoOptions& other) const {
        return seed == other.seed && rateCoefficient == other.rateCoefficient &&
               rateExponent == other.rateExponent && lifetimeMyr == other.lifetimeMyr &&
               minDiameter == other.minDiameter && maxDiameter == other.maxDiameter &&
               darkShare == other.darkShare && brightShare == other.brightShare &&
               darkChange == other.darkChange && brightChange == other.brightChange &&
               steepBrightShare == other.steepBrightShare &&
               steepSlopeDegrees == other.steepSlopeDegrees && softness == other.softness &&
               raggedness == other.raggedness && fade == other.fade && swarms == other.swarms;
    }
    bool operator!=(const SplotchAlbedoOptions& other) const {
        return !(*this == other);
    }
};

/// Checks splotch options.
/// @throws std::invalid_argument for a non-positive rate, slope or lifetime, diameters that are
/// not positive and increasing, shares outside [0, 1] or summing above 1, changes that can reach
/// -1 or are unbounded, a softness outside (0, 1], a raggedness outside [0, 1), a steep bright
/// share outside [0, 1], a steep slope outside (0, 90) degrees, an invalid fade, or swarms with
/// a non-positive rate, slope, count or range, crater diameters or ranges not increasing, a mu
/// outside (0, 1), invalid or unbounded distributions, shares outside [0, 1] or summing above 1,
/// or a wedge elongation below 1.
void ValidateSplotchAlbedoOptions(const SplotchAlbedoOptions& options);

/// One splotch.
struct Splotch {
    Crater mark;               ///< Center, diameter, age and ID, as a crater's.
    double change = 0.0;       ///< Reflectance change of its dark or only side.
    double otherChange = 0.0;  ///< Reflectance change of its other side; `change` unless mixed.
    double split = 0.0;        ///< Direction across which a mixed splotch changes side, radians.
    std::uint32_t key = 0;     ///< Key of its edge's lobes.
    double elongation = 1.0;   ///< Long over short axis; 1 for round.
    double heading = 0.0;      ///< Toward the crater that threw it, radians from east.
    bool wedge = false;        ///< Narrowing to a point toward the crater.
};

/// The splotches of `SplotchAlbedoOptions` on `surface`, darkening and brightening the albedo; the
/// same in any grid, tile or point.
class SplotchAlbedo final : public AlbedoLayer {
  public:
    /// @throws std::invalid_argument for options invalid as `ValidateSplotchAlbedoOptions`.
    explicit SplotchAlbedo(SplotchAlbedoOptions options = {}, TerrainBase base = {});

    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const AlbedoContext& context) const override;

    void Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const override;

    /// The splotches resolved at `spacing` meters reaching `region`, youngest first; with
    /// `ground`, those on steep slopes follow `steepBrightShare`.
    std::vector<Splotch> Splotches(const Bounds2d& region, double spacing,
                                   const Ground* ground = nullptr) const;

    /// The splotches of the swarm `crater` throws, resolved at `spacing` meters (0 for all).
    std::vector<Splotch> Swarm(const Crater& crater, double spacing) const;

    /// The relative reflectance change `splotch` alone gives (`x`, `y`) at `spacing` meters.
    double Change(const Splotch& splotch, double x, double y, double spacing) const;

    /// What the splotches are.
    const SplotchAlbedoOptions& Options() const {
        return options_;
    }

  private:
    /// The splotches of `region` resolved at `spacing` meters, before their sides are drawn.
    std::vector<Splotch> Marks(const Bounds2d& region, double spacing) const;
    /// The slope in degrees of `ground` under each of `splotches`, over its width, read at
    /// `spacing` meters (half the smallest splotch for 0); all 0 without `ground` or a steep
    /// bright share.
    std::vector<double> SteepSlopes(const std::vector<Splotch>& splotches, double spacing,
                                    const Ground* ground) const;
    /// Draws whether `splotch` on ground of `slopeDegrees` is dark, bright or mixed, and its
    /// changes, split and lobes.
    void DrawSides(Splotch& splotch, double slopeDegrees) const;
    /// Adds the splotches of the swarms reaching `region` to `splotches`.
    void AddSwarms(const Bounds2d& region, double spacing, std::vector<Splotch>& splotches) const;
    /// The `index`th splotch of the swarm `crater` throws, its draws the next of `random`.
    Splotch DrawSwarmSplotch(RandomStream& random, const Crater& crater, std::uint64_t index) const;
    /// The change of `splotches`, youngest first, at (`x`, `y`).
    double ChangeAt(const std::vector<const Splotch*>& splotches, double x, double y,
                    double spacing) const;

    SplotchAlbedoOptions options_;
    Surface surface_;
    std::shared_ptr<const SurfaceCells> cells_;
};

}  // namespace terraingen
