// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief How a `BoulderLayer` sizes, places, poses and draws its boulders.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/boulders/boulder_models.h"
#include "terraingen/layers/boulders/boulder_shape.h"
#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/density_map.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

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

    /// Whether a boulder of `diameter` meters is a model: at or above a lower threshold and below
    /// the upper ones.
    /// @param spacing Sample spacing in meters; 0 for none, when only `minDiameter` counts.
    bool IsModel(double diameter, double spacing) const;

    bool operator==(const BoulderModelThreshold& other) const {
        return minDiameter == other.minDiameter && minSamples == other.minSamples &&
               maxDiameter == other.maxDiameter && maxSamples == other.maxSamples;
    }
};

/// The boulder sizes a layer has by default between craters and around them: the nominal NASA
/// SP-8023 intercrater blocks (`Sp8023IntercraterBlocks`, Li et al. 2017,
/// https://doi.org/10.1016/j.pss.2017.08.008), one instance shared by every default.
std::shared_ptr<const SizeFrequencyDistribution> DefaultBoulderSizes();

/// The sizes of the boulders between craters: `distribution`, or Golombek and Rapp (1997)
/// boulders of `coverage` without one, from `minDiameter` up to `maxDiameter`.
struct BoulderSizes {
    /// Golombek-Rapp k, the fractional area covered by boulders, for sizes without a
    /// `distribution`; unset is 0.02 (`Coverage`).
    std::optional<double> coverage{};
    /// Golombek-Rapp q = qa + qb / k per meter, for Golombek-Rapp sizes here or in the ejecta:
    /// qa, unset 1.79 (`QConstant`).
    std::optional<double> qConstant{};
    std::optional<double> qCoverageCoefficient{};  ///< qb, unset 0.152 (`QCoverageCoefficient`).
    /// Sizes per m^2, by default `DefaultBoulderSizes`; null for Golombek-Rapp boulders of
    /// `coverage`. Patches and bedrock scale its density, or k without it.
    std::shared_ptr<const SizeFrequencyDistribution> distribution = DefaultBoulderSizes();
    double minDiameter = 0.1;  ///< Smallest boulder (m).
    /// Largest boulder (m), exclusive; by default the largest block of `DefaultBoulderSizes`.
    double maxDiameter = DefaultBoulderSizes()->ValidRange().max;

    /// The Golombek-Rapp k: `coverage`, or 0.02 unset.
    double Coverage() const {
        return coverage.value_or(0.02);
    }
    /// The Golombek-Rapp qa: `qConstant`, or 1.79 unset.
    double QConstant() const {
        return qConstant.value_or(1.79);
    }
    /// The Golombek-Rapp qb: `qCoverageCoefficient`, or 0.152 unset.
    double QCoverageCoefficient() const {
        return qCoverageCoefficient.value_or(0.152);
    }

    bool operator==(const BoulderSizes& other) const {
        return coverage == other.coverage && qConstant == other.qConstant &&
               qCoverageCoefficient == other.qCoverageCoefficient &&
               distribution == other.distribution && minDiameter == other.minDiameter &&
               maxDiameter == other.maxDiameter;
    }
};

/// Patches of richer and poorer boulder cover.
struct BoulderPatches {
    /// Abundance varies from 1 / `spread` to `spread` times its mean; 1 for uniform.
    double spread = 1.0;
    double size = 200.0;  ///< Typical width of a patch (m).

    bool operator==(const BoulderPatches& other) const {
        return spread == other.spread && size == other.size;
    }
};

/// How boulder cluster centers are laid out and how their clusters combine.
enum class BoulderClusterModel {
    /// One center per cell of a jittered lattice `spacing` apart; each point takes the Gaussian of
    /// its nearest center only.
    Nearest,
    /// A Thomas cluster process: centers scattered as a Poisson process, one per `spacing`^2 on
    /// average, each spreading its boulders in a Gaussian of `radius`, overlapping clusters adding
    /// (Thomas 1949, https://doi.org/10.1093/biomet/36.1-2.18).
    Thomas,
};

/// Boulders between craters gathered in clusters: `share` of them crowd within about `radius` of
/// cluster centers about `spacing` apart and the rest stay scattered. The mean abundance is kept:
/// the mean count with a size distribution, the mean coverage with Golombek-Rapp boulders.
struct BoulderClusters {
    double share = 0.0;     ///< Share of the boulders in clusters, in [0, 1]; 0 for none.
    double spacing = 50.0;  ///< Typical distance between cluster centers (m).
    /// Gaussian radius of a cluster (m): below `spacing` for `Nearest`, at most twice it for
    /// `Thomas`.
    double radius = 5.0;
    BoulderClusterModel model = BoulderClusterModel::Nearest;  ///< How the clusters are laid out.

    bool operator==(const BoulderClusters& other) const {
        return share == other.share && spacing == other.spacing && radius == other.radius &&
               model == other.model;
    }
};

/// Boulders broken by wear into pieces that replace them. From `splitWear` a boulder at least
/// `minDiameter` across is split by near-vertical cracks into pieces that still fit, their gaps
/// opening to `gapRatio` of its diameter; from `rubbleWear` the pieces scatter up to `spreadRatio`
/// diameters further, turn and sink.
struct BoulderFragmentation {
    double minDiameter = 1.0;  ///< Smallest boulder that breaks (m).
    double splitWear = 0.5;    ///< Wear from which boulders are split, in [0, 1].
    double rubbleWear = 1.0;   ///< Wear from which the pieces scatter, at least `splitWear`.
    int minPieces = 2;         ///< Fewest pieces, at least 2.
    int maxPieces = 5;         ///< Most pieces, at most 12.
    double gapRatio = 0.04;    ///< Widest crack between split pieces, over the diameter.
    double spreadRatio = 0.6;  ///< How far rubble scatters beyond that, over the diameter.
    /// Share of broken boulders split into slabs by roughly parallel cracks right through them; the
    /// rest crack out from the middle like a pie.
    double slabShare = 0.7;

    bool operator==(const BoulderFragmentation& other) const {
        return minDiameter == other.minDiameter && splitWear == other.splitWear &&
               rubbleWear == other.rubbleWear && minPieces == other.minPieces &&
               maxPieces == other.maxPieces && gapRatio == other.gapRatio &&
               spreadRatio == other.spreadRatio && slabShare == other.slabShare;
    }
};

/// How the share of a crater's blocks falls off with distance from its center, in radii.
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
/// largest share. They are generated boulders, so a layer without `generateRandom` has none.
struct BoulderEjecta {
    /// Craters at least this large throw blocks out (m); unset, with no `regolithThickness`, no
    /// crater throws blocks.
    std::optional<double> minCraterDiameter{};
    /// The regolith's thickness (m): craters smaller than `regolithThickness /
    /// excavationDepthRatio` do not dig through it and throw no blocks.
    std::optional<double> regolithThickness{};
    double excavationDepthRatio = 0.1;  ///< Depth of excavation over crater diameter.
    /// The blocks' sizes at the rim of a fresh crater, per m^2, by default `DefaultBoulderSizes`;
    /// null for Golombek-Rapp boulders of `coverage`.
    std::shared_ptr<const SizeFrequencyDistribution> distribution = DefaultBoulderSizes();
    /// Golombek-Rapp k at the rim of a fresh crater, for blocks without a `distribution`; unset is
    /// 0.1 (`Coverage`).
    std::optional<double> coverage{};
    EjectaProfile profile = EjectaProfile::Smoothstep;  ///< Falloff with distance.
    double reach = 3.0;        ///< Radii from the center beyond which a crater adds no blocks.
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
    /// regolith; unset when no crater throws blocks.
    std::optional<double> ThrowingDiameter() const;

    /// The Golombek-Rapp k at the rim: `coverage`, or 0.1 unset.
    double Coverage() const {
        return coverage.value_or(0.1);
    }

    bool operator==(const BoulderEjecta& other) const {
        return minCraterDiameter == other.minCraterDiameter &&
               regolithThickness == other.regolithThickness &&
               excavationDepthRatio == other.excavationDepthRatio &&
               distribution == other.distribution && coverage == other.coverage &&
               profile == other.profile && reach == other.reach && decayRadii == other.decayRadii &&
               floorShare == other.floorShare && aging == other.aging &&
               minFreshness == other.minFreshness && lifetimeGyr == other.lifetimeGyr &&
               agingExponent == other.agingExponent && gravity == other.gravity;
    }
};

/// The greatest throw (m) of blocks from a crater's center over a regolith blanket: Bart and
/// Melosh's (2010, eq. 9) (138.5 / g) (D / H)^0.7086.
/// @param craterDiameter D, in meters.
/// @param regolithThickness H, in meters.
/// @param gravity g, in m/s^2.
/// @throws std::invalid_argument unless every argument is positive and finite.
double BoulderThrowDistance(double craterDiameter, double regolithThickness, double gravity);

/// Boulders breaking out of bedrock: abundance scales from 1 on soil to `abundance` on
/// bedrock, in proportion to the exposure.
struct BoulderBedrock {
    /// The exposure field; unset for no bedrock boulders.
    std::optional<ExposureParams> exposure{};
    double abundance = 3.0;  ///< Abundance on bedrock over that on soil.
    /// With a density map, the side (m) of the squares bedrock is held to the map's mean over, so
    /// it gathers boulders without adding any; a raster map's own cells are used instead.
    double meanSize = 1000.0;

    bool operator==(const BoulderBedrock& other) const {
        return exposure == other.exposure && abundance == other.abundance &&
               meanSize == other.meanSize;
    }
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
    /// Tips each boulder model over, from its drawn pose, until it rests stably on level ground
    /// (`PlaceBoulderModel`), before the tilt and the slope; off leaves shapes as drawn, some
    /// balanced on an edge or corner.
    bool settle = true;
    /// Steepest ground, measured across a boulder's diameter, that boulders stay on (radians);
    /// boulders on steeper ground are left out, models and boulders drawn into the heights alike,
    /// so which boulders exist does not depend on the spacing. Unset keeps boulders on any slope;
    /// needs the ground.
    std::optional<double> maxSlope{};
    /// With `maxSlope`, boulders on steeper ground roll down it instead, in steps along the
    /// steepest descent, and come to rest where it is no steeper than `maxSlope`, gathering at the
    /// foot of slopes; those that would roll farther than this many meters are left out. Unset
    /// leaves out every boulder on steeper ground.
    std::optional<double> rollDistance{};

    bool operator==(const BoulderPose& other) const {
        return minHeightRatio == other.minHeightRatio && maxHeightRatio == other.maxHeightRatio &&
               minBurial == other.minBurial && maxBurial == other.maxBurial &&
               maxLean == other.maxLean && burialByWear == other.burialByWear &&
               sideShare == other.sideShare && endShare == other.endShare &&
               maxTilt == other.maxTilt && followSlope == other.followSlope &&
               settle == other.settle && maxSlope == other.maxSlope &&
               rollDistance == other.rollDistance;
    }
};

/// Regolith piled against each boulder's base, drawn into the heights around boulders drawn
/// there and around boulder models alike: `heightShare` of the exposed height out to the boulder's
/// radius, then falling as (1 - t)^2 to nothing `widthRatio` diameters further out. Where the
/// boulder stands above it, the boulder shows.
struct BoulderFillet {
    double heightShare = 0.0;  ///< Height over the boulder's exposed height, in [0, 1]; 0 for none.
    double widthRatio = 0.3;   ///< Reach beyond the boulder's radius, over its diameter.

    bool operator==(const BoulderFillet& other) const {
        return heightShare == other.heightShare && widthRatio == other.widthRatio;
    }
};

/// Ventifacts: windward facets planed on boulders by sand-laden wind, set once the boulder has
/// settled. A facet faces upwind at `minAngle` to `maxAngle` from horizontal and meets the ground
/// at the boulder's upwind edge when fresh, moving in to its center as it wears; boulders with a
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

    bool operator==(const BoulderVentifacts& other) const {
        return share == other.share && windAzimuth == other.windAzimuth &&
               minAngle == other.minAngle && maxAngle == other.maxAngle &&
               secondWindAzimuth == other.secondWindAzimuth && keelShare == other.keelShare;
    }
};

/// @throws std::invalid_argument for a share or keel share outside [0, 1], facet angles outside
/// (0, 90) deg or out of order, or non-finite wind azimuths.
void ValidateBoulderVentifacts(const BoulderVentifacts& ventifacts);

/// The ventifacts of Gale crater's Bradbury Landing to Rocknest traverse: half the rocks are
/// ventifacts and 78% of those faceted, 39% faceted in all, 12 of the 39 faceted with keels
/// (Bridges et al. 2014), and facets of 50 to 60 deg as on basalt (Laity and Bridges 2009). The
/// winds are left to the site: Gale's came from the southwest and north-northeast.
BoulderVentifacts GaleVentifacts();

/// The height (m) of `boulder`'s fillet `distance` meters from its center.
double FilletHeight(const Boulder& boulder, double distance, const BoulderFillet& fillet);

/// How far (m) from its center `boulder`'s fillet reaches: its radius plus the fillet's width.
double FilletReach(const Boulder& boulder, const BoulderFillet& fillet);

/// Which boulders are drawn into the heights: those from `minDiameter` up to `maxDiameter` that are
/// not models, and models too with `models`. Boulders drawn neither way stay in the lists only,
/// such as those too small to matter at the grid's scale.
struct BoulderReliefRange {
    double minDiameter = 0.0;             ///< Smallest boulder drawn (m).
    std::optional<double> maxDiameter{};  ///< Boulders this wide (m) or wider are not drawn.
    bool models = false;  ///< Draw models into the heights as well, as a stand-in for far views.

    /// Whether a boulder of `diameter` meters, a model or not, is drawn into the heights.
    bool Holds(double diameter, bool model) const {
        return diameter >= minDiameter && (!maxDiameter || diameter < *maxDiameter) &&
               (!model || models);
    }

    bool operator==(const BoulderReliefRange& other) const {
        return minDiameter == other.minDiameter && maxDiameter == other.maxDiameter &&
               models == other.models;
    }
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

    bool operator==(const BoulderRendering& other) const {
        return relief == other.relief && models == other.models &&
               reliefRange == other.reliefRange && shapes == other.shapes &&
               shapePrototypes == other.shapePrototypes;
    }
};

/// How a `BoulderLayer` sizes, places, poses and draws its boulders.
struct BoulderLayerOptions {
    /// Draws random boulders from `sizes` between craters and from `ejecta` around them; off
    /// places only the `manual` boulders, draws no candidates and no crater blocks, and leaves
    /// `sizes`, `ejecta` and the abundance settings unchecked and unused.
    bool generateRandom = true;
    BoulderSizes sizes{};            ///< Sizes of the boulders between craters.
    std::uint32_t seed = 1;          ///< Varies the boulders.
    BoulderPatches patches{};        ///< Richer and poorer patches.
    BoulderClusters clusters{};      ///< Clusters between craters; none by default.
    BoulderEjecta ejecta{};          ///< Blocks around fresh craters.
    BoulderBedrock bedrock{};        ///< Extra boulders on bedrock.
    BoulderPose pose{};              ///< Heights, burial, lean.
    BoulderFillet fillet{};          ///< Regolith piled against each boulder; none by default.
    BoulderVentifacts ventifacts{};  ///< Facets planed by the wind; none by default.
    std::optional<BoulderFragmentation> fragmentation{};  ///< Boulders broken by wear; none unset.
    BoulderRendering rendering{};                         ///< Relief or models, and shapes.
    /// Which boulders are drawn into the heights by their size in samples of the evaluation
    /// spacing: none narrower than `startSamples` (2, the fewest that resolve one), in full from
    /// `endSamples`. Grids, tiles and points at one spacing agree; the lists and models hold
    /// every boulder whatever its size.
    ResolutionFade fade{};
    /// Spacing (m) at which the layer reads the ground beneath it, for the exposure field,
    /// placement, settling, slopes and breaking; fixed so that which boulders exist and how they
    /// sit does not depend on the level of detail.
    double groundSpacing = 1.0;
    /// Most boulder candidates to keep generated cells of; 0 regenerates cells every time.
    std::size_t cacheBoulders = 4000000;
    /// A map that sets the abundance of boulders between craters point by point, measuring the
    /// boulder sizes (`DensityMap`); with `total` the blocks around craters are taken out of what
    /// it measures first. Patches, clusters and bedrock still spread boulders within it. Unset
    /// keeps the abundance `sizes` sets everywhere.
    std::optional<DensityMap> densityMap{};
    /// A map of how steeply boulder counts fall with size (`SizeSlopeMap`), such as a mean
    /// diameter map, bending the sizes between craters into a local power law through their count
    /// at its reference diameter; needs a size distribution rather than Golombek-Rapp sizes. Unset
    /// keeps the sizes' own shape everywhere.
    std::optional<SizeSlopeMap> slopeMap{};
    /// Boulders from a catalog or placed by hand, placed as given and never thinned or left out;
    /// IDs of 0 come from position and size (`ManualCraterId`). Fields they leave unset are drawn
    /// as for generated boulders; those fixing any field are never broken, and those fixing their
    /// orientation are not tipped over by `pose.settle`.
    std::vector<ManualBoulder> manual{};
    /// The diameter (m) from which `manual` is a complete catalog at each point: generated boulders
    /// at least this large centered there are dropped, so the catalog stands in for them. Infinite
    /// or NaN where there is no catalog; unset keeps every generated boulder.
    std::shared_ptr<const SurfaceField> completeness{};
    /// Source cells across from which the source's measured data already holds a boulder: with a
    /// source given to the layer, generated boulders at least that many of its cells across are
    /// dropped (`SourceResolvedLimit`), the minimum with `completeness` when both are set. Unset,
    /// or without a source, keeps every generated boulder.
    std::optional<double> sourceResolvesPixels{};

    bool operator==(const BoulderLayerOptions& other) const {
        return generateRandom == other.generateRandom && sizes == other.sizes &&
               seed == other.seed && patches == other.patches && clusters == other.clusters &&
               ejecta == other.ejecta && bedrock == other.bedrock && pose == other.pose &&
               fillet == other.fillet && ventifacts == other.ventifacts &&
               fragmentation == other.fragmentation && rendering == other.rendering &&
               fade == other.fade && groundSpacing == other.groundSpacing &&
               cacheBoulders == other.cacheBoulders && densityMap == other.densityMap &&
               slopeMap == other.slopeMap && manual == other.manual &&
               completeness == other.completeness &&
               sourceResolvesPixels == other.sourceResolvesPixels;
    }
};

/// Checks boulder patches.
/// @throws std::invalid_argument unless `spread` >= 1 is finite and `size` positive.
void ValidateBoulderPatches(const BoulderPatches& patches);

/// Checks boulder clusters.
/// @throws std::invalid_argument unless the share is in [0, 1], the spacing is finite and the
/// radius positive, below the spacing for `Nearest` and at most twice it for `Thomas`.
void ValidateBoulderClusters(const BoulderClusters& clusters);

/// Checks boulder fragmentation.
/// @throws std::invalid_argument unless the minimum diameter is positive, 0 <= `splitWear` <=
/// `rubbleWear` <= 1, 2 <= `minPieces` <= `maxPieces` <= 12, and the ratios are finite and not
/// negative.
void ValidateBoulderFragmentation(const BoulderFragmentation& fragmentation);

/// Checks boulder fillets.
/// @throws std::invalid_argument unless the height share is in [0, 1] and the width ratio is
/// positive and finite.
void ValidateBoulderFillet(const BoulderFillet& fillet);

/// Checks a manual boulder.
/// @throws std::invalid_argument unless the position is finite, the diameter positive and finite,
/// and each set field valid: the height positive and finite, the burial in [0, 1), the angles
/// finite and the wear in [0, 1].
void ValidateManualBoulder(const ManualBoulder& boulder);

/// The ID a manual boulder is placed with: its own, or `ManualCraterId` of its position and size
/// when it has none (or 0).
std::uint64_t ManualBoulderId(const ManualBoulder& boulder);

/// Checks boulder layer options; without `generateRandom`, the sizes, ejecta and the coverage they
/// give are not checked.
/// @throws std::invalid_argument for any invalid part, a non-positive ground spacing,
/// Golombek-Rapp coverage times the patch spread and bedrock abundance of 1 or more, a
/// Golombek-Rapp term set beside the size distribution that leaves it unused, or manual boulders
/// sharing an ID.
void ValidateBoulderLayerOptions(const BoulderLayerOptions& options);

}  // namespace terraingen
