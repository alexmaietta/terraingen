#pragma once

#include "terraingen/distribution/golombek_rapp.h"
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
    /// Coverage k varies from k / `spread` to k * `spread`; 1 is the same k everywhere.
    double spread = 1.0;
    double size = 200.0;  ///< Typical width of a patch (m).

    bool operator==(const RockPatches&) const = default;
};

/// Blocks thrown out by craters that dug through the regolith into rock: densest at the rim and
/// thinning outward, and broken down over time, so few are left around old craters.
///
/// Reference: J. A. Watkins, C. I. Fassett et al., "Boulder distributions around young, small lunar
/// impact craters and implications for regolith production rates and landing site safety", JGR
/// Planets 124, 2019. https://doi.org/10.1029/2019JE005963 (the shares here are not theirs).
struct RockEjecta {
    double coverage = 0.1;  ///< Coverage k at the rim of a fresh crater.
    double minCraterDiameter =
        0.0;             ///< Craters at least this large throw rocks out (m); 0 disables.
    double reach = 3.0;  ///< Radii from the centre at which the cover falls back to its own.
    double minFreshness =
        0.7;  ///< Craters at or below this freshness (depth / fresh depth) throw none.

    bool operator==(const RockEjecta&) const = default;
};

/// Settings for a `RockLayer`.
struct RockLayerOptions {
    double coverage = 0.02;  ///< Golombek-Rapp k, the fractional area covered by rocks.
    double qa = 1.79;        ///< Golombek-Rapp q = qa + qb / k, per metre.
    double qb = 0.152;
    double minDiameter = 0.1;  ///< Smallest rock (m).
    double maxDiameter = 4.0;  ///< Largest rock (m), exclusive.
    std::uint32_t seed = 1;
    RockPatches patches{};
    RockEjecta ejecta{};
    /// Rocks break out of bedrock: where the exposure field says bedrock, the cover rises halfway
    /// from its k toward the richest k of the patches (or of the ejecta). Unset for none.
    std::optional<ExposureParams> exposure;
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

/// The richest coverage k anywhere under `options`: that of the richest patches, or of fresh crater
/// rims when ejecta are enabled. Candidate rocks are drawn at it before thinning.
double RockCandidateCoverage(const RockLayerOptions& options);

/// Rocks scattered over the ground: Golombek and Rapp (1997) sizes from a coverage k that varies in
/// patches, rises toward the rims of fresh craters (`ejecta`, which needs `craters`) and where
/// bedrock shows (`exposure`, which reads the ground beneath the layer).
///
/// Candidates are drawn per cell in size bands like craters, at the richest k anywhere, from a seed
/// stream of their own, then each is kept with probability n(D; k(x)) / n(D; k_max), from a uniform
/// keyed by its ID: exact thinning, since n(D; k) grows with k at every D. Each rock's height,
/// burial, yaw and lean come from its ID too. Everything depends only on position, so rocks are the
/// same in every grid, tile and point query; bands too small to resolve at the spacing are not
/// generated, and the rocks of the bands kept are the same as at any finer spacing.
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

    /// The coverage k at (`x`, `y`): the patches, the ejecta of fresh craters and the bedrock.
    double CoverageAt(double x, double y, const Ground* ground = nullptr) const;

    double MaxFeatureDiameter() const override {
        return options_.maxDiameter;
    }

    const RockLayerOptions& Options() const {
        return options_;
    }

  private:
    /// The ejecta craters that can raise the cover in `region`.
    CraterList EjectaCraters(const Bounds2d& region) const;
    double Coverage(double x, double y, const Ground* ground, const CraterList& ejecta) const;

    RockLayerOptions options_;
    std::shared_ptr<const CraterLayer> craters_;
    double maxCoverage_ = 0.0;
    std::shared_ptr<const GolombekRappSfd> candidates_;
    std::shared_ptr<CraterCellCache> cache_;
    std::optional<ExposureField> exposure_;
};

/// `height` at (`x`, `y`) after the exposed part of `rock`, a dome of the rock's exposed height
/// over its diameter, weighted by `weight`; what `RockLayer` adds to each cell.
float AddRock(float height, double x, double y, const Rock& rock, double weight);

}  // namespace terraingen
