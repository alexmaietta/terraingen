#pragma once

#include "terraingen/distribution/golombek_rapp.h"
#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/apply.h"
#include "terraingen/layers/craters/cell_cache.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/rocks/rock.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// What a rock layer adds to the heights.
enum class RockRelief {
    Body,  ///< Each rock's exposed part, as a dome: for heightmaps that should show the rocks.
    None,  ///< Nothing: for programs that place each rock as a body or mesh of its own.
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
    RockRelief relief = RockRelief::Body;
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
    RockLayer(RockLayerOptions options, std::shared_ptr<const CraterLayer> craters = nullptr);

    /// Adds the rocks' exposed parts (with `RockRelief::Body`) and records the rocks centred in the
    /// grid or within a radius of it.
    /// @throws std::invalid_argument if the exposure field is enabled and `context.ground` is null.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// The rocks reaching (`x`, `y`) added to `height` there, to the same bits as a cell centred
    /// there; NaN stays NaN.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    /// The rocks centred in `region` or within their radius of it, at least resolvable at `spacing`
    /// (0 for every rock), in a fixed order; `ground` is needed when the exposure field is enabled.
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

  private:
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
    std::optional<ExposureField> exposure_;
};

/// `height` at (`x`, `y`) after the exposed part of `rock`, a dome of the rock's exposed height
/// over its diameter, weighted by `weight`; what `RockLayer` adds to each cell.
float AddRock(float height, double x, double y, const Rock& rock, double weight);

}  // namespace terraingen
