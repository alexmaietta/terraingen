#pragma once

#include "terraingen/distribution/golombek_rapp.h"
#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/apply.h"
#include "terraingen/layers/craters/cell_cache.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/rocks/boulder_meshes.h"
#include "terraingen/layers/rocks/rock.h"
#include "terraingen/util/lru_cache.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// How the rocks drawn into the heights look there.
enum class RockRelief {
    Dome,   ///< Each rock's exposed part as a dome: cheap, and enough for small rocks.
    Shape,  ///< The top surface of each rock's 3D boulder shape (`GenerateBoulderShape`).
    None,   ///< Nothing: for programs that place every rock as a model of its own.
};

/// Which rocks become 3D models of their own rather than relief in the heights: those at least
/// `minDiameter` metres across, or spanning at least `minSamples` samples at the spacing evaluated.
/// Rocks too small to resolve are left out (`ResolutionFade`), rocks from there up to the threshold
/// are drawn into the heights, and rocks above it are models, which suit them better than a DEM
/// does. Neither set means no models. With `minSamples`, a rock drawn into a coarse level of detail
/// becomes a model at a finer one.
struct RockModels {
    std::optional<double> minDiameter{};
    std::optional<double> minSamples{};

    /// Whether a rock `diameter` metres across is a model at `spacing` (0 for no spacing, when only
    /// `minDiameter` counts).
    bool IsModel(double diameter, double spacing) const;

    bool operator==(const RockModels&) const = default;
};

/// Patches of richer and poorer rock cover.
struct RockPatches {
    /// The abundance of rocks varies from 1 / `spread` to `spread` times its mean; 1 is the same
    /// everywhere.
    double spread = 1.0;
    double size = 200.0;  ///< Typical width of a patch (m).

    bool operator==(const RockPatches&) const = default;
};

/// How the share of a crater's blocks falls off with distance from its centre, in crater radii.
enum class EjectaProfile {
    /// Every block inside the rim, falling along a smoothstep to none at `reach`.
    Smoothstep,
    /// exp(-(rho - 1) / `decayRadii`) outside the rim, `floorShare` of the rim's on the floor, and
    /// none from `reach`: synthterrain's crater probability map (decay 0.7, floor 0.05, reach 2).
    Exponential,
};

/// How a crater's blocks wear away with time.
enum class EjectaAging {
    /// By the crater's freshness (depth now over depth when fresh): all from 1, none at or below
    /// `minFreshness`, along a smoothstep.
    Freshness,
    /// By the crater's age: (1 - age / `lifetimeGyr`)^`agingExponent`, synthterrain's form
    /// (lifetime 4 Gyr, exponent 3).
    Age,
};

/// Blocks thrown out by craters that dug through the regolith into rock, added to the rocks
/// between craters: densest at the rim and thinning outward, and broken down over time, so few are
/// left around old craters. Where the ejecta of several craters overlap, the largest share counts.
///
/// References:
/// - J. A. Watkins, C. I. Fassett et al., "Boulder distributions around young, small lunar impact
///   craters and implications for regolith production rates and landing site safety", JGR Planets
///   124, 2019. https://doi.org/10.1029/2019JE005963
/// - G. D. Bart and H. J. Melosh, "Distributions of boulders ejected from lunar craters", Icarus
///   209:337-357, 2010. https://doi.org/10.1016/j.icarus.2010.05.023
/// - H. J. Melosh, "Impact Cratering: A Geologic Process", Oxford University Press, 1989 (depth of
///   excavation about a tenth of the diameter).
struct RockEjecta {
    /// Craters at least this large throw blocks out (m); 0, with no `regolithThickness`, disables
    /// crater blocks.
    double minCraterDiameter = 0.0;
    /// The regolith's thickness (m): craters too small to dig through it, smaller than
    /// `regolithThickness / excavationDepthRatio`, throw no blocks.
    std::optional<double> regolithThickness;
    double excavationDepthRatio = 0.1;  ///< Depth of excavation over crater diameter.
    /// The blocks' sizes at the rim of a fresh crater, per m^2; unset for Golombek-Rapp rocks of
    /// `coverage`, as with `Sp8023CraterBlocks` otherwise.
    std::shared_ptr<const SizeFrequencyDistribution> distribution{};
    double coverage = 0.1;  ///< Golombek-Rapp k at the rim of a fresh crater.
    EjectaProfile profile = EjectaProfile::Smoothstep;
    double reach = 3.0;        ///< Radii from the centre beyond which a crater adds no blocks.
    double decayRadii = 0.7;   ///< e-folding distance of `Exponential`, in radii.
    double floorShare = 0.05;  ///< The floor's share of the rim's blocks with `Exponential`.
    EjectaAging aging = EjectaAging::Freshness;
    double minFreshness = 0.7;  ///< Freshness at or below which a crater keeps no blocks.
    double lifetimeGyr = 4.0;   ///< Age at which a crater keeps no blocks, for `Age`.
    double agingExponent = 3.0;
    /// Surface gravity (m/s^2): when set with `regolithThickness`, a crater's blocks reach no
    /// further than Bart and Melosh's (2010b, eq. 9) greatest throw over a regolith blanket,
    /// (138.5 / g) (D / H)^0.7086 m from the centre (`BoulderThrowDistance`).
    std::optional<double> gravity;

    /// The smallest crater that throws blocks (m), from `minCraterDiameter` and the regolith; 0
    /// when crater blocks are disabled.
    double MinCraterDiameter() const;

    bool operator==(const RockEjecta&) const = default;
};

/// How far, in metres, blocks are thrown from a crater `craterDiameter` across in rock under
/// `regolithThickness` metres of regolith, at surface gravity `gravity`: (138.5 / g) (D /
/// H)^0.7086.
///
/// Reference: G. D. Bart and H. J. Melosh, "Impact into lunar regolith inhibits high-velocity
/// ejection of large blocks", JGR Planets 115, E08004, 2010, eq. 9.
/// https://doi.org/10.1029/2009JE003441
/// @throws std::invalid_argument unless every argument is positive and finite.
double BoulderThrowDistance(double craterDiameter, double regolithThickness, double gravity);

/// Settings for a `RockLayer`.
struct RockLayerOptions {
    double coverage = 0.02;  ///< Golombek-Rapp k, the fractional area covered by rocks.
    double qa = 1.79;        ///< Golombek-Rapp q = qa + qb / k, per metre.
    double qb = 0.152;
    /// The sizes of the rocks between craters, per m^2, in place of Golombek-Rapp rocks of
    /// `coverage`: `Sp8023IntercraterBlocks` or `SynthterrainRockDistribution`, say. Patches and
    /// bedrock scale its density; with Golombek-Rapp rocks they scale k.
    std::shared_ptr<const SizeFrequencyDistribution> distribution{};
    double minDiameter = 0.1;  ///< Smallest rock (m).
    double maxDiameter = 4.0;  ///< Largest rock (m), exclusive.
    std::uint32_t seed = 1;
    RockPatches patches{};
    RockEjecta ejecta{};
    /// Rocks break out of bedrock: where the exposure field says bedrock, the abundance of rocks is
    /// `bedrockAbundance` times that on soil, and in between in proportion. Unset for none.
    std::optional<ExposureParams> exposure;
    double bedrockAbundance = 3.0;
    /// The spacing the ground is read at for the exposure field, fixed so that which rocks exist
    /// does not depend on the level of detail (m).
    double exposureSpacing = 1.0;
    double minHeightRatio = 0.4;  ///< Rock height over diameter, uniform between these.
    double maxHeightRatio = 0.6;
    double minBurial = 0.1;  ///< Buried share of the height, uniform between these.
    double maxBurial = 0.4;
    double maxLean = 0.26;  ///< Largest tilt from the vertical (radians, about 15 degrees).
    RockRelief relief = RockRelief::Dome;
    RockModels models{};
    /// The 3D shapes of the rocks, for `RockRelief::Shape` and for models.
    BoulderShapeOptions shapes{};
    /// How many shapes the rocks share, so that they can be drawn as instances; 0 gives every rock
    /// a shape of its own (`BoulderShapeSeed`).
    std::uint32_t shapePrototypes = 64;
    ResolutionFade fade{};
    /// Most rock candidates to keep generated cells of; 0 regenerates cells every time.
    std::size_t cacheRocks = 4000000;

    bool operator==(const RockLayerOptions&) const = default;
};

/// @throws std::invalid_argument for invalid options.
void ValidateRockLayerOptions(const RockLayerOptions& options);

/// The distribution candidate rocks are drawn from before thinning: the rocks between craters at
/// their richest, plus fresh crater blocks when enabled, so that no point has more of any size.
/// @throws std::invalid_argument for invalid options.
std::shared_ptr<const SizeFrequencyDistribution> RockCandidates(const RockLayerOptions& options);

/// How rich the rocks are at a point.
struct RockAbundance {
    /// The density of the rocks between craters over its mean: the patches and the bedrock.
    double background = 1.0;
    /// The share of fresh crater blocks: from the profile and age of the craters around.
    double ejecta = 0.0;

    bool operator==(const RockAbundance&) const = default;
};

/// Rocks scattered over the ground: rocks between craters, Golombek and Rapp (1997) sizes or any
/// size-frequency distribution, whose abundance varies in patches and rises where bedrock shows
/// (`exposure`, which reads the ground beneath the layer), plus the blocks around fresh craters
/// (`ejecta`, which needs `craters`).
///
/// Candidates are drawn per cell in size bands like craters, from `RockCandidates`, with a seed
/// stream of their own; each is then kept with probability n(D; x) / n_candidates(D), from a
/// uniform keyed by its ID: exact thinning, since no point has more rocks of any size than the
/// candidates. Each rock's height, burial, yaw and lean come from its ID too. Everything depends
/// only on position, so rocks are the same in every grid, tile and point query; bands too small to
/// resolve at the spacing are not generated, and the rocks of the bands kept are the same as at
/// any finer spacing.
class RockLayer final : public Layer {
  public:
    /// `craters` supplies the craters that throw rocks out; it may be null when ejecta are
    /// disabled.
    /// @throws std::invalid_argument for invalid options, or ejecta without `craters`.
    ///
    /// On a sphere (`surface`, which must be the crater layer's), candidates are drawn in
    /// `CubeSphereCells`, patches are 3D noise, distances are great-circle, each rock's relief is
    /// shaped in its local frame, and boulder model placements are in the rock's local east, north
    /// and up frame, its origin on the datum below the rock.
    RockLayer(RockLayerOptions options, std::shared_ptr<const CraterLayer> craters = nullptr,
              Surface surface = {});

    /// Adds the exposed parts of the rocks drawn into the heights, as `relief` says, and records
    /// the rocks centred in the grid or within a radius of it, the models among them with their
    /// shapes placed on the ground beneath (the ground from `context`, otherwise the heights as
    /// given). The ground under a rock is read at its centre.
    /// @throws std::invalid_argument if the exposure field is enabled and `context.ground` is null.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// The rocks drawn into the heights that reach (`x`, `y`) added to `height` there, to the same
    /// bits as a cell centred there; NaN stays NaN.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    /// The rocks centred in `region` or within their radius of it, at least resolvable at `spacing`
    /// (0 for every rock), in a fixed order, each marked as a model or not at that spacing and with
    /// the height of `ground` under it when given; `ground` is needed when the exposure field is
    /// enabled.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    std::vector<Rock> Rocks(const Bounds2d& region, double spacing = 0.0,
                            const Ground* ground = nullptr) const;

    /// How rich the rocks are at (`x`, `y`).
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    RockAbundance AbundanceAt(double x, double y, const Ground* ground = nullptr) const;

    /// -dN/dD of the rocks at (`x`, `y`), per m^2 per metre of diameter, for `diameter` within
    /// [`minDiameter`, `maxDiameter`).
    double DensityAt(double diameter, double x, double y, const Ground* ground = nullptr) const;

    double MaxFeatureDiameter() const override {
        return options_.maxDiameter;
    }

    const RockLayerOptions& Options() const {
        return options_;
    }

    /// The 3D shape of `rock`, shared between rocks with the same shape seed.
    std::shared_ptr<const BoulderShape> Shape(const Rock& rock) const;

  private:
    /// The rock's exposed part added to `height` at (`x`, `y`), with `placed` its shape placed on
    /// level ground at 0 for `RockRelief::Shape`.
    float AddRelief(float height, double x, double y, const Rock& rock, const TriangleMesh* placed,
                    double weight) const;
    /// `rock`'s shape placed on level ground at 0.
    TriangleMesh PlacedOnLevel(const Rock& rock) const;

    /// The ejecta craters that can add blocks in `region`.
    CraterList EjectaCraters(const Bounds2d& region) const;
    RockAbundance Abundance(double x, double y, const Ground* ground,
                            const CraterList& ejecta) const;
    double Density(double diameter, const RockAbundance& abundance) const;

    RockLayerOptions options_;
    std::shared_ptr<const CraterLayer> craters_;
    double minCraterDiameter_ = 0.0;
    double maxBackground_ = 1.0;
    std::shared_ptr<const SizeFrequencyDistribution> candidates_;
    std::shared_ptr<const SizeFrequencyDistribution> ejectaSizes_;
    std::shared_ptr<CraterCellCache> cache_;
    std::shared_ptr<LruCache<std::uint64_t, BoulderShape>> shapes_;
    std::optional<ExposureField> exposure_;
    Surface surface_;
    std::shared_ptr<const SurfaceCells> cells_;
};

/// `height` at (`x`, `y`) after the exposed part of `rock`, a dome of the rock's exposed height
/// over its diameter, weighted by `weight`; what `RockLayer` adds to each cell.
float AddRock(float height, double x, double y, const Rock& rock, double weight,
              const Surface& surface = {});

}  // namespace terraingen
