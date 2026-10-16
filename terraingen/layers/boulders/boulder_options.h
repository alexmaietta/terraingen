/// @file
/// @brief How a `BoulderLayer` sizes, places, poses and draws its boulders.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/boulders/boulder_models.h"
#include "terraingen/layers/boulders/boulder_shape.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace terraingen {

/// How the boulders drawn into the heights look there.
enum class BoulderRelief {
    Dome,   ///< Each boulder's exposed part as a dome: cheap, and enough for small boulders.
    Shape,  ///< The top surface of each boulder's 3D boulder shape (`GenerateBoulderShape`).
    /// No rock, only fillets: for programs that place every boulder as a model of its own.
    None,
};

/// Which boulders become 3D models of their own rather than relief in the heights; neither
/// threshold set means no models.
struct BoulderModelThreshold {
    std::optional<double> minDiameter{};  ///< Boulders at least this wide (m) are models.
    std::optional<double> minSamples{};   ///< Boulders spanning at least this many samples are.

    /// Whether a boulder of `diameter` metres is a model: above either threshold.
    /// @param spacing Sample spacing in metres; 0 for none, when only `minDiameter` counts.
    bool IsModel(double diameter, double spacing) const;

    bool operator==(const BoulderModelThreshold&) const = default;
};

/// The sizes of the boulders between craters: Golombek and Rapp (1997) boulders of
/// `coverage`, or `distribution`, from `minDiameter` up to `maxDiameter`.
struct BoulderSizes {
    double coverage = 0.02;  ///< Golombek-Rapp k, the fractional area covered by boulders.
    /// Golombek-Rapp q = `qConstant` + `qCoverageCoefficient` / k, per metre: their qa and qb.
    double qConstant = 1.79;
    double qCoverageCoefficient = 0.152;  ///< Golombek-Rapp qb (see `qConstant`).
    /// Sizes per m^2 in place of Golombek-Rapp boulders (e.g. `Sp8023IntercraterBlocks`); patches
    /// and bedrock scale its density, or k without it.
    std::shared_ptr<const SizeFrequencyDistribution> distribution{};
    double minDiameter = 0.1;  ///< Smallest boulder (m).
    double maxDiameter = 4.0;  ///< Largest boulder (m), exclusive.

    bool operator==(const BoulderSizes&) const = default;
};

/// Patches of richer and poorer boulder cover.
struct BoulderPatches {
    /// Abundance varies from 1 / `spread` to `spread` times its mean; 1 for uniform.
    double spread = 1.0;
    double size = 200.0;  ///< Typical width of a patch (m).

    bool operator==(const BoulderPatches&) const = default;
};

/// How the share of a crater's blocks falls off with distance from its centre, in radii.
enum class EjectaProfile {
    /// Every block inside the rim, falling along a smoothstep to none at `reach`.
    Smoothstep,
    /// exp(-(rho - 1) / `decayRadii`) outside the rim, `floorShare` of the rim's on the floor, and
    /// none from `reach`: synthterrain's crater probability map (decay 0.7, floor 0.05, reach 2).
    Exponential,
};

/// How a crater's blocks wear away with time.
enum class EjectaAging {
    /// By the crater's freshness (`CraterState::Freshness`): all from 1, none at or below
    /// `minFreshness`, along a smoothstep.
    Freshness,
    /// By the crater's age: (1 - age / `lifetimeGyr`)^`agingExponent`, synthterrain's form
    /// (lifetime 4 Gyr, exponent 3).
    Age,
};

/// Blocks thrown out by craters that dug through the regolith, added to the boulders
/// between craters: densest at the rim, thinning outward and with age; overlapping ejecta take the
/// largest share.
struct BoulderEjecta {
    /// Craters at least this large throw blocks out (m); 0, with no `regolithThickness`, disables
    /// crater blocks.
    double minCraterDiameter = 0.0;
    /// The regolith's thickness (m): craters smaller than `regolithThickness /
    /// excavationDepthRatio` do not dig through it and throw no blocks.
    std::optional<double> regolithThickness{};
    double excavationDepthRatio = 0.1;  ///< Depth of excavation over crater diameter.
    /// The blocks' sizes at the rim of a fresh crater, per m^2; unset for Golombek-Rapp boulders of
    /// `coverage`.
    std::shared_ptr<const SizeFrequencyDistribution> distribution{};
    double coverage = 0.1;  ///< Golombek-Rapp k at the rim of a fresh crater.
    EjectaProfile profile = EjectaProfile::Smoothstep;  ///< Falloff with distance.
    double reach = 3.0;        ///< Radii from the centre beyond which a crater adds no blocks.
    double decayRadii = 0.7;   ///< e-folding distance of `Exponential`, in radii.
    double floorShare = 0.05;  ///< The floor's share of the rim's blocks with `Exponential`.
    EjectaAging aging = EjectaAging::Freshness;  ///< Wear with time.
    double minFreshness = 0.7;   ///< Freshness at or below which a crater keeps no blocks.
    double lifetimeGyr = 4.0;    ///< Age at which a crater keeps no blocks, for `Age`.
    double agingExponent = 3.0;  ///< Exponent of the `Age` wear.
    /// Surface gravity (m/s^2): when set with `regolithThickness`, a crater's blocks reach no
    /// further than `BoulderThrowDistance`.
    std::optional<double> gravity{};

    /// The diameter (m) of the smallest crater that throws blocks, from `minCraterDiameter` and the
    /// regolith; 0 when crater blocks are disabled.
    double MinCraterDiameter() const;

    bool operator==(const BoulderEjecta&) const = default;
};

/// The greatest throw (m) of blocks from a crater's centre over a regolith blanket: Bart and
/// Melosh's (2010, eq. 9) (138.5 / g) (D / H)^0.7086.
/// @param craterDiameter D, in metres.
/// @param regolithThickness H, in metres.
/// @param gravity g, in m/s^2.
/// @throws std::invalid_argument unless every argument is positive and finite.
double BoulderThrowDistance(double craterDiameter, double regolithThickness, double gravity);

/// Boulders breaking out of bedrock: abundance scales from 1 on soil to `abundance` on
/// bedrock, in proportion to the exposure.
struct BoulderBedrock {
    /// The exposure field; unset for no bedrock boulders.
    std::optional<ExposureParams> exposure{};
    double abundance = 3.0;  ///< Abundance on bedrock over that on soil.
    /// Spacing the ground is read at for the exposure field (m), fixed so that which boulders exist
    /// does not depend on the level of detail.
    double exposureSpacing = 1.0;

    bool operator==(const BoulderBedrock&) const = default;
};

/// How each boulder stands, drawn per boulder uniformly between these.
struct BoulderPose {
    double minHeightRatio = 0.4;  ///< Least boulder height over diameter.
    double maxHeightRatio = 0.6;  ///< Greatest boulder height over diameter.
    double minBurial = 0.1;       ///< Least buried share of the height.
    double maxBurial = 0.4;       ///< Greatest buried share of the height.
    double maxLean = 0.26;        ///< Largest tilt from the vertical (radians, about 15 degrees).
    /// How far wear sets burial, from 0 to 1: the buried share is drawn at (1 - b) u + b w of the
    /// way from `minBurial` to `maxBurial`, with u uniform and w the boulder's wear. A crater's
    /// blocks wear with it (0 fresh to 1 gone); boulders between craters span every stage, their
    /// wear uniform. 0 draws burial uniformly.
    double burialByWear = 0.0;

    bool operator==(const BoulderPose&) const = default;
};

/// Regolith piled against each boulder's base, drawn into the heights around boulders drawn
/// there and around boulder models alike: `heightShare` of the exposed height out to the boulder's
/// radius, then falling as (1 - t)^2 to nothing `widthRatio` diameters further out. Where the
/// boulder stands above it, the boulder shows.
struct BoulderFillet {
    double heightShare = 0.0;  ///< Height over the boulder's exposed height, in [0, 1]; 0 for none.
    double widthRatio = 0.3;   ///< Reach beyond the boulder's radius, over its diameter.

    bool operator==(const BoulderFillet&) const = default;
};

/// The height (m) of `boulder`'s fillet `distance` metres from its centre.
double FilletHeight(const Boulder& boulder, double distance, const BoulderFillet& fillet);

/// How far (m) from its centre `boulder`'s fillet reaches: its radius plus the fillet's width.
double FilletReach(const Boulder& boulder, const BoulderFillet& fillet);

/// How boulders are drawn: as relief in the heights or as 3D models, and with which shapes.
struct BoulderRendering {
    BoulderRelief relief = BoulderRelief::Dome;  ///< Look of boulders drawn into the heights.
    BoulderModelThreshold models{};              ///< Which boulders are models instead.
    /// The 3D shapes of the boulders, for `BoulderRelief::Shape` and for models.
    BoulderShapeOptions shapes{};
    /// How many shapes the boulders share, for instancing; 0 gives every boulder its own
    /// (`BoulderShapeSeed`).
    std::uint32_t shapePrototypes = 64;

    bool operator==(const BoulderRendering&) const = default;
};

/// How a `BoulderLayer` sizes, places, poses and draws its boulders.
struct BoulderLayerOptions {
    BoulderSizes sizes{};          ///< Sizes of the boulders between craters.
    std::uint32_t seed = 1;        ///< Varies the boulders.
    BoulderPatches patches{};      ///< Richer and poorer patches.
    BoulderEjecta ejecta{};        ///< Blocks around fresh craters.
    BoulderBedrock bedrock{};      ///< Extra boulders on bedrock.
    BoulderPose pose{};            ///< Heights, burial, lean.
    BoulderFillet fillet{};        ///< Regolith piled against each boulder; none by default.
    BoulderRendering rendering{};  ///< Relief or models, and shapes.
    ResolutionFade fade{};         ///< Fade of boulders too small for the spacing.
    /// Most boulder candidates to keep generated cells of; 0 regenerates cells every time.
    std::size_t cacheBoulders = 4000000;

    bool operator==(const BoulderLayerOptions&) const = default;
};

/// Checks boulder sizes.
/// @throws std::invalid_argument unless 0 < `minDiameter` < `maxDiameter` are finite, and either
/// the distribution is a density covering the diameters (or extrapolating) or 0 < `coverage` < 1.
void ValidateBoulderSizes(const BoulderSizes& sizes);

/// Checks boulder patches.
/// @throws std::invalid_argument unless `spread` >= 1 is finite and `size` positive.
void ValidateBoulderPatches(const BoulderPatches& patches);

/// Checks crater blocks.
/// @param sizes The boulder sizes the blocks' distribution must cover.
/// @throws std::invalid_argument for a negative `minCraterDiameter`, a non-positive regolith
/// thickness, an excavation depth ratio outside (0, 1], and when blocks are enabled, an invalid
/// distribution or coverage, a reach of 1 radius or less, a non-positive decay, a floor share
/// outside [0, 1], a freshness outside [0, 1), a non-positive lifetime, a negative aging exponent
/// or a non-positive gravity.
void ValidateBoulderEjecta(const BoulderEjecta& ejecta, const BoulderSizes& sizes);

/// Checks bedrock boulders.
/// @throws std::invalid_argument for a non-positive abundance or exposure spacing, or invalid
/// exposure parameters.
void ValidateBoulderBedrock(const BoulderBedrock& bedrock);

/// Checks boulder poses.
/// @throws std::invalid_argument unless 0 < min <= max height ratio, 0 <= min <= max burial < 1,
/// the lean is finite and not negative, and the burial by wear is in [0, 1].
void ValidateBoulderPose(const BoulderPose& pose);

/// Checks boulder fillets.
/// @throws std::invalid_argument unless the height share is in [0, 1] and the width ratio is
/// positive and finite.
void ValidateBoulderFillet(const BoulderFillet& fillet);

/// Checks how boulders are drawn.
/// @throws std::invalid_argument for a non-positive model threshold or invalid shapes.
void ValidateBoulderRendering(const BoulderRendering& rendering);

/// Checks boulder layer options.
/// @throws std::invalid_argument for any invalid part, or Golombek-Rapp coverage times the patch
/// spread and bedrock abundance of 1 or more.
void ValidateBoulderLayerOptions(const BoulderLayerOptions& options);

/// The largest background abundance, on the richest patches on bedrock: the patch spread, times
/// the bedrock abundance (at least 1) when bedrock is enabled.
double MaxBackgroundAbundance(const BoulderLayerOptions& options);

/// The sizes of the crater blocks at a fresh crater's rim: the ejecta distribution, or
/// Golombek-Rapp blocks of the ejecta coverage.
std::shared_ptr<const SizeFrequencyDistribution> EjectaSizes(const BoulderLayerOptions& options);

/// The distribution per m^2 candidate boulders are drawn from before thinning: the boulders
/// between craters at their richest plus fresh crater blocks when enabled.
/// @throws std::invalid_argument as `ValidateBoulderLayerOptions`.
std::shared_ptr<const SizeFrequencyDistribution>
BoulderCandidates(const BoulderLayerOptions& options);

}  // namespace terraingen
