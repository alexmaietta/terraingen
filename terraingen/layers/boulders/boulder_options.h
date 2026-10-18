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

/// Which boulders become 3D models of their own: those at or above either lower threshold and
/// below every upper one set; no lower threshold means no models.
struct BoulderModelThreshold {
    std::optional<double> minDiameter{};  ///< Boulders at least this wide (m) are models.
    std::optional<double> minSamples{};   ///< Boulders spanning at least this many samples are.
    std::optional<double> maxDiameter{};  ///< Boulders this wide (m) or wider are not.
    std::optional<double> maxSamples{};   ///< Boulders spanning this many samples or more are not.

    /// Whether a boulder of `diameter` metres is a model: at or above a lower threshold and below
    /// the upper ones.
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

/// How boulder cluster centres are laid out and how their clusters combine.
enum class BoulderClusterModel {
    /// One centre per cell of a jittered lattice `spacing` apart; each point takes the Gaussian of
    /// its nearest centre only.
    Nearest,
    /// A Thomas cluster process: centres scattered as a Poisson process, one per `spacing`^2 on
    /// average, each spreading its boulders in a Gaussian of `radius`, overlapping clusters adding
    /// (Thomas 1949, https://doi.org/10.1093/biomet/36.1-2.18).
    Thomas,
};

/// Boulders between craters gathered in clusters: `share` of them crowd within about `radius` of
/// cluster centres about `spacing` apart and the rest stay scattered. The mean abundance is kept:
/// the mean count with a size distribution, the mean coverage with Golombek-Rapp boulders.
struct BoulderClusters {
    double share = 0.0;     ///< Share of the boulders in clusters, in [0, 1]; 0 for none.
    double spacing = 50.0;  ///< Typical distance between cluster centres (m).
    /// Gaussian radius of a cluster (m): below `spacing` for `Nearest`, at most twice it for
    /// `Thomas`.
    double radius = 5.0;
    BoulderClusterModel model = BoulderClusterModel::Nearest;  ///< How the clusters are laid out.

    bool operator==(const BoulderClusters&) const = default;
};

/// Boulders broken by wear into pieces that replace them. From `splitWear` a boulder at least
/// `minDiameter` across is split by near-vertical cracks into pieces that still fit, their gaps
/// opening to `gapRatio` of its diameter; from `rubbleWear` the pieces scatter up to `spreadRatio`
/// diameters further, turn and sink. A split wear of 1 breaks nothing.
struct BoulderFragmentation {
    double minDiameter = 1.0;  ///< Smallest boulder that breaks (m).
    double splitWear = 1.0;    ///< Wear from which boulders are split, in [0, 1].
    double rubbleWear = 1.0;   ///< Wear from which the pieces scatter, at least `splitWear`.
    int minPieces = 2;         ///< Fewest pieces, at least 2.
    int maxPieces = 5;         ///< Most pieces, at most 12.
    double gapRatio = 0.04;    ///< Widest crack between split pieces, over the diameter.
    double spreadRatio = 0.6;  ///< How far rubble scatters beyond that, over the diameter.
    /// Share of broken boulders split into slabs by roughly parallel cracks right through them; the
    /// rest crack out from the middle like a pie.
    double slabShare = 0.7;

    /// Whether any boulder breaks.
    bool Enabled() const {
        return splitWear < 1.0;
    }

    bool operator==(const BoulderFragmentation&) const = default;
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
    /// Share of boulders resting on a side, their intermediate axis up, rather than their face.
    double sideShare = 0.0;
    /// Share of boulders standing on an end, their long axis up; with `sideShare`, at most 1.
    double endShare = 0.0;
    /// Largest tilt of a boulder's up axis from the vertical in a random direction, after its lean
    /// (radians); drawn uniformly in the cosine, so steep tilts are as likely as on a sphere cap.
    double maxTilt = 0.0;
    /// Settles each boulder on the slope of the ground beneath it, its up axis along the ground's
    /// normal before the tilt; needs the ground.
    bool followSlope = false;

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

/// Ventifacts: windward facets planed on boulders by sand-laden wind, set once the boulder has
/// settled. A facet faces upwind at `minAngle` to `maxAngle` from horizontal and meets the ground
/// at the boulder's upwind edge when fresh, moving in to its centre as it wears; boulders with a
/// keel carry a second facet facing the second wind. Angles are in radians.
struct BoulderVentifacts {
    double share = 0.0;  ///< Share of boulders with a facet, in [0, 1]; 0 for none.
    /// Where the wind blows toward, radians counter-clockwise from east.
    double windAzimuth = 0.0;
    double minAngle = 0.8726646259971648;  ///< Gentlest facet slope; 50 deg.
    double maxAngle = 1.0471975511965976;  ///< Steepest facet slope; 60 deg.
    /// A second formative wind, where it blows toward; unset gives no keels.
    std::optional<double> secondWindAzimuth{};
    /// Share of faceted boulders with a second facet and a keel between, in [0, 1].
    double keelShare = 0.0;

    bool operator==(const BoulderVentifacts&) const = default;
};

/// @throws std::invalid_argument for a share or keel share outside [0, 1], facet angles outside
/// (0, 90) deg or out of order, or non-finite wind azimuths.
void ValidateBoulderVentifacts(const BoulderVentifacts& ventifacts);

/// The ventifacts of Gale crater's Bradbury Landing to Rocknest traverse: half the rocks are
/// ventifacts and 78% of those faceted, 39% faceted in all, 12 of the 39 faceted with keels
/// (Bridges et al. 2014), and facets of 50 to 60 deg as on basalt (Laity and Bridges 2009). The
/// winds are left to the site: Gale's came from the southwest and north-northeast.
BoulderVentifacts GaleVentifacts();

/// The height (m) of `boulder`'s fillet `distance` metres from its centre.
double FilletHeight(const Boulder& boulder, double distance, const BoulderFillet& fillet);

/// How far (m) from its centre `boulder`'s fillet reaches: its radius plus the fillet's width.
double FilletReach(const Boulder& boulder, const BoulderFillet& fillet);

/// Which boulders are drawn into the heights: those from `minDiameter` up to `maxDiameter` that are
/// not models, and models too with `models`. Boulders drawn neither way stay in the lists only,
/// such as those too small to matter at the grid's scale.
struct BoulderReliefRange {
    double minDiameter = 0.0;             ///< Smallest boulder drawn (m).
    std::optional<double> maxDiameter{};  ///< Boulders this wide (m) or wider are not drawn.
    bool models = false;  ///< Draw models into the heights as well, as a stand-in for far views.

    /// Whether a boulder of `diameter` metres, a model or not, is drawn into the heights.
    bool Holds(double diameter, bool model) const {
        return diameter >= minDiameter && (!maxDiameter || diameter < *maxDiameter) &&
               (!model || models);
    }

    bool operator==(const BoulderReliefRange&) const = default;
};

/// How boulders are drawn: as relief in the heights or as 3D models, and with which shapes.
struct BoulderRendering {
    BoulderRelief relief = BoulderRelief::Dome;  ///< Look of boulders drawn into the heights.
    BoulderModelThreshold models{};              ///< Which boulders are 3D models.
    BoulderReliefRange reliefRange{};            ///< Which boulders are drawn into the heights.
    /// The 3D shapes of the boulders, for `BoulderRelief::Shape` and for models.
    BoulderShapeOptions shapes{};
    /// How many shapes the boulders share, for instancing; 0 gives every boulder its own
    /// (`BoulderShapeSeed`).
    std::uint32_t shapePrototypes = 64;

    bool operator==(const BoulderRendering&) const = default;
};

/// How a `BoulderLayer` sizes, places, poses and draws its boulders.
struct BoulderLayerOptions {
    BoulderSizes sizes{};            ///< Sizes of the boulders between craters.
    std::uint32_t seed = 1;          ///< Varies the boulders.
    BoulderPatches patches{};        ///< Richer and poorer patches.
    BoulderClusters clusters{};      ///< Clusters between craters; none by default.
    BoulderEjecta ejecta{};          ///< Blocks around fresh craters.
    BoulderBedrock bedrock{};        ///< Extra boulders on bedrock.
    BoulderPose pose{};              ///< Heights, burial, lean.
    BoulderFillet fillet{};          ///< Regolith piled against each boulder; none by default.
    BoulderVentifacts ventifacts{};  ///< Facets planed by the wind; none by default.
    BoulderFragmentation fragmentation{};  ///< Boulders broken by wear; none by default.
    BoulderRendering rendering{};          ///< Relief or models, and shapes.
    ResolutionFade fade{};                 ///< Fade of boulders too small for the spacing.
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
/// the lean is finite and not negative, the burial by wear is in [0, 1], the side and end shares
/// are not negative and sum to at most 1, and the tilt is in [0, pi].
void ValidateBoulderPose(const BoulderPose& pose);

/// Checks boulder clusters.
/// @throws std::invalid_argument unless the share is in [0, 1], the spacing is finite and the
/// radius positive, below the spacing for `Nearest` and at most twice it for `Thomas`.
void ValidateBoulderClusters(const BoulderClusters& clusters);

/// A cluster's weight `distance` metres from its centre: a Gaussian of the cluster's radius.
double ClusterBump(double distance, const BoulderClusters& clusters);

/// The distance in radii beyond which a `Thomas` cluster adds nothing.
inline constexpr double kThomasReachRadii = 4.0;

/// The mean of `ClusterBump` of the nearest cluster centre over the plane, which the cluster field
/// is divided by to keep the mean density.
double MeanClusterBump(const BoulderClusters& clusters);

/// The mean over the surface of the `Thomas` field, the sum of `ClusterBump` within
/// `kThomasReachRadii` of every centre, which it is divided by to keep the mean density.
/// @param body Whether centres are scattered through space around a body, `spacing`^2 per square
/// metre of a plane through them, rather than over a plane.
double MeanThomasField(const BoulderClusters& clusters, bool body = false);

/// The largest a cluster field multiplies the scattered boulder density by; 1 without clusters.
/// For `Thomas` it is where as many centres crowd within 3 radii as a one in a million chance
/// allows, and the rare points above it are capped to it.
double MaxClusterAbundance(const BoulderClusters& clusters);

/// Checks boulder fragmentation.
/// @throws std::invalid_argument unless the minimum diameter is positive, 0 <= `splitWear` <=
/// `rubbleWear` <= 1, 2 <= `minPieces` <= `maxPieces` <= 12, and the ratios are finite and not
/// negative.
void ValidateBoulderFragmentation(const BoulderFragmentation& fragmentation);

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
