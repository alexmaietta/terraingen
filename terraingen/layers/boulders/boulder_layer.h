/// @file
/// @brief `BoulderLayer`, boulders scattered over the ground and around fresh craters.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/boulders/boulder_abundance.h"
#include "terraingen/layers/boulders/boulder_fragments.h"
#include "terraingen/layers/boulders/boulder_models.h"
#include "terraingen/layers/boulders/boulder_options.h"
#include "terraingen/layers/boulders/boulder_placer.h"
#include "terraingen/layers/boulders/ejecta_index.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/generation/cell_cache.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/material.h"
#include "terraingen/util/lru_cache.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// How rich the boulders are at a point.
struct BoulderAbundance {
    /// Density of the boulders between craters over its mean, from patches and bedrock.
    double background = 1.0;
    /// Share of fresh crater blocks, from the profile and age of the craters around.
    double ejecta = 0.0;
    /// Wear of the crater the ejecta share comes from, from 0 (fresh) to 1 (its blocks gone).
    double ejectaWear = 0.0;
    /// The size exponent a size slope map gives here (`SizeSlopeAt`); none keeps the sizes' shape.
    std::optional<double> exponent;

    bool operator==(const BoulderAbundance&) const = default;
};

/// The share of a sample's area covered by boulders too small to resolve in it.
struct UnresolvedCover {
    double mean = 0.0;      ///< Expected share.
    double variance = 0.0;  ///< Variance of the share from the boulders' Poisson scatter.

    bool operator==(const UnresolvedCover&) const = default;
};

/// Boulders scattered over the ground: between craters, in patches and richer on bedrock,
/// plus blocks around fresh craters (which need `craters`).
///
/// Candidates are drawn per cell in size bands and thinned by a uniform keyed by each ID, so
/// boulders depend only on position and agree on every grid, tile and point.
class BoulderLayer final : public Layer, public BoulderPlacer, public MaterialSource {
  public:
    /// On a sphere, candidates are drawn in `CubeSphereCells`, distances are great-circle, and
    /// relief and model placements are in each boulder's local east, north and up frame, its
    /// origin on the datum below the boulder.
    /// @param craters The crater layer whose craters throw blocks; may be null without ejecta.
    /// @param surface The crater layer's surface.
    /// @throws std::invalid_argument for invalid options, ejecta without `craters`, or a crater
    /// layer on another surface.
    BoulderLayer(BoulderLayerOptions options, std::shared_ptr<const CraterLayer> craters = nullptr,
                 Surface surface = {});

    /// The boulders centred in `region` (surface coordinates) or reaching it with their radius or
    /// fillet, in a fixed order.
    /// @param spacing Sample spacing in metres; boulders too small to resolve at it are left out
    /// and models are marked at it; 0 keeps every boulder.
    /// @param ground Read under each boulder; needed with the exposure field.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    std::vector<Boulder> Boulders(const Bounds2d& region, double spacing = 0.0,
                                  const Ground* ground = nullptr) const;

    /// The background and ejecta abundance at surface (x, y).
    /// @param ground Needed with the exposure field.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    BoulderAbundance AbundanceAt(double x, double y, const Ground* ground = nullptr) const;

    /// The density -dN/dD of boulders of `diameter` at surface (x, y), per m^2 per metre of
    /// diameter.
    /// @param diameter In metres, within the layer's sizes.
    /// @param ground Needed with the exposure field.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    double DensityAt(double diameter, double x, double y, const Ground* ground = nullptr) const;

    /// The share of a sample `spacing` metres across at surface (x, y) that the layer's boulders
    /// cover where they are too small to resolve, each weighted by one minus its
    /// `ResolutionWeight`: the area their density integrates to, and the variance of that share
    /// over the sample's area for Poisson-scattered boulders. Visible plan areas, as the
    /// Golombek-Rapp coverage.
    /// @param spacing Positive.
    /// @param ground Needed with the exposure field.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    UnresolvedCover UnresolvedCoverAt(double x, double y, double spacing,
                                      const Ground* ground = nullptr) const;
    /// `UnresolvedCoverAt` at every cell centre of `grid`, row-major, the ejecta craters found
    /// once.
    /// @throws std::invalid_argument as `UnresolvedCoverAt`.
    std::vector<UnresolvedCover> UnresolvedCoverOver(const RasterGrid& grid, double spacing,
                                                     const Ground* ground, unsigned threads) const;

    /// `sizes.maxDiameter`.
    double MaxFeatureDiameter() const {
        return options_.sizes.maxDiameter;
    }

    /// The options the layer was made with.
    const BoulderLayerOptions& Options() const {
        return options_;
    }

    /// The 3D shape of a boulder, shared between boulders with the same shape seed; for a
    /// fragment, its piece of its parent's shape.
    std::shared_ptr<const BoulderShape> Shape(const Boulder& boulder) const;

    /// The boulders of `boulders` to place as their own 3D models: those marked `model` whose
    /// ground is known, placed on it.
    std::vector<BoulderModel> Models(const std::vector<Boulder>& boulders) const;

    BoulderFeatures BouldersOver(const RasterGrid& grid, double spacing,
                                 const Ground* ground) const override;

    MaterialClass Material() const override {
        return MaterialClass::Rock;
    }
    /// The weight at `spacing` of the boulder whose rock stands at surface (x, y), a model's over
    /// its footprint; 0 between boulders and on fillets.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    double MaterialCoverAt(double x, double y, double spacing, const Ground* ground) const override;
    /// `MaterialCoverAt` over `grid`, the boulders found once for the whole grid.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    Raster<double> MaterialCoverOver(const RasterGrid& grid, double spacing, const Ground* ground,
                                     unsigned threads) const override;

  protected:
    /// Draws the exposed parts of non-model boulders into the samples.
    /// @throws std::invalid_argument if the exposure field is enabled and `context.ground` is null.
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    /// Builds the background abundance models, the candidate sizes and the cluster mean.
    void SetUpAbundance();
    /// Sets the lattice bedrock means are taken over and their cache.
    void SetUpBedrockMeans();
    /// Makes the caches of shared shapes and their fragments.
    void SetUpShapeCaches();
    /// The candidates of `region`, before thinning.
    CraterList ScatterBoulderCandidates(const Bounds2d& region, double spacing) const;
    /// A candidate thinning keeps, with what it was kept by.
    struct Kept {
        Crater candidate;
        BoulderAbundance abundance;
        double density = 0.0;
    };
    /// `candidate` with its abundance and density, or nothing when thinning drops it.
    std::optional<Kept> Keep(const Crater& candidate, const Ground* ground,
                             const EjectaIndex& ejecta) const;
    /// A kept candidate as a boulder, or nothing when it is left out on steep ground.
    std::optional<Boulder> Place(const Kept& kept, double spacing, const Ground* ground) const;
    /// The boulders `candidates` give, in their order: those thinning keeps, their ground read in
    /// one pass (`Ground::HeightsAt`) and then placed.
    std::vector<Boulder> DrawBoulders(const CraterList& candidates, double spacing,
                                      const Ground* ground, const EjectaIndex& ejecta) const;
    /// The points `GroundGradient` reads around (`x`, `y`) for a boulder `diameter` metres across:
    /// east, west, north and south.
    std::array<Vec2d, 4> GradientPoints(double x, double y, double diameter) const;
    /// Margin in radii a boulder's own relief and fillet reach beyond its centre.
    double OwnMarginRadii() const;
    /// Margin in radii its pieces may scatter beyond that when boulders break.
    double BreakMarginRadii() const;
    /// Sets `boulder`'s resting axis and tilt from the pose options, on the slope of `ground` when
    /// they follow it.
    void Settle(Boulder& boulder, const Ground* ground) const;
    /// The rise east and north (m/m) of `ground` across `boulder`'s diameter; none where the
    /// ground is missing.
    std::optional<Vec2d> GroundGradient(const Boulder& boulder, const Ground& ground) const;
    /// Rolls `boulder` down ground steeper than `maxSlope` until it rests, as `rollDistance`
    /// allows; false when it would roll farther, or with no roll distance on steep ground.
    bool Roll(Boulder& boulder, const Ground& ground) const;
    /// The pieces `parent` breaks into at its wear, those resolved at `spacing`; `parent` alone
    /// when it is too small or too fresh to break.
    std::vector<Boulder> Break(const Boulder& parent, double spacing, const Ground* ground) const;
    /// The pieces of the shape of seed `shapeSeed`, shared by every boulder of that shape.
    std::shared_ptr<const std::vector<BoulderFragment>> FragmentsOf(std::uint64_t shapeSeed) const;
    /// The shape of seed `shapeSeed`.
    std::shared_ptr<const BoulderShape> ShapeOf(std::uint64_t shapeSeed) const;
    /// The wear (0 fresh to 1 worn) of a boulder of `diameter` keyed by `id`: that of the crater
    /// its block came from, drawn by each source's share of `density`, or uniform between craters.
    double Wear(double diameter, const BoulderAbundance& abundance, double density,
                std::uint64_t id) const;
    /// `height` at (`x`, `y`) raised to at least `base` (the height before any boulder) plus the
    /// boulder's exposed part and its fillet, so boulders and fillets form a union instead of
    /// stacking; `placed` is its shape placed on level ground at 0 for `BoulderRelief::Shape`, a
    /// dome is averaged over `width` metres, and without `rock` only the fillet is drawn.
    float AddRelief(float height, float base, double x, double y, const Boulder& boulder,
                    const TriangleMesh* placed, double weight, bool rock, double width) const;
    /// The height of `boulder`'s exposed rock above the ground at (`x`, `y`), 0 off it; `placed`
    /// and `width` as in `AddRelief`.
    double RockRelief(double x, double y, const Boulder& boulder, const TriangleMesh* placed,
                      double width) const;
    /// `boulder` in its local frame: centred at the origin on a sphere, unchanged on a plane.
    Boulder InLocalFrame(const Boulder& boulder) const;
    /// `boulder`'s shape placed on level ground at 0, cut by its facets.
    TriangleMesh PlacedOnLevel(const Boulder& boulder) const;
    /// Gives `boulder` its ventifact facets, drawn by its ID.
    void Abrade(Boulder& boulder) const;
    /// Moves the points of `placed` (a mesh in `local`'s frame, on ground at height `base`) above
    /// `local`'s facets onto them.
    void CutFacets(TriangleMesh& placed, const Boulder& local, double base) const;
    /// `shape` cut by `local`'s facets where `placement` puts it, still in the shape's own frame.
    BoulderShape Abraded(const BoulderShape& shape, const BoulderPlacement& placement,
                         const Boulder& local, double base) const;
    /// A boulder drawn into the heights: its weight at the spacing, whether its rock is drawn or
    /// only its fillet, its shape placed on level ground (for `BoulderRelief::Shape`) and the box
    /// its relief covers.
    struct Relief {
        const Boulder* boulder;
        double weight;
        bool rock;
        double width;  ///< Width a dome is averaged over: the spacing, or 0 for shapes.
        std::optional<TriangleMesh> placed;
        Bounds2d box;
    };
    /// The boulders of `boulders` resolved at `spacing` with relief or a fillet to draw, in order.
    std::vector<Relief> PrepareRelief(const std::vector<Boulder>& boulders, double spacing) const;
    /// Draws `reliefs` into every sample their boxes hold.
    void DrawRelief(SampleBlock& samples, const std::vector<Relief>& reliefs,
                    unsigned threads) const;

    /// The ejecta craters that can add blocks in `region`, indexed by how far their blocks reach.
    EjectaIndex EjectaCraters(const Bounds2d& region) const;
    /// The abundance at (`x`, `y`) with the ejecta craters `ejecta` around.
    BoulderAbundance Abundance(double x, double y, const Ground* ground,
                               const EjectaIndex& ejecta) const;
    /// The distance (m) from surface (x, y) to the nearest cluster centre.
    double NearestClusterDistance(double x, double y) const;
    /// The sum at surface (x, y) of the Gaussians of the `Thomas` cluster centres around it.
    double ThomasClusterField(double x, double y) const;
    /// The abundance between craters at (`x`, `y`), where the crater blocks hold `ejecta` of
    /// theirs.
    double BackgroundAbundance(double x, double y, const Ground* ground, double ejecta) const;
    /// Sets `abundance`'s ejecta share and wear at (`x`, `y`) from the craters `ejecta`.
    void EjectaAbundance(double x, double y, const EjectaIndex& ejecta,
                         BoulderAbundance& abundance) const;
    double Density(double diameter, const BoulderAbundance& abundance) const;
    /// A diameter `UnresolvedCoverAt` integrates over, its plan area, and its quadrature weight
    /// times the share of it left unresolved.
    struct UnresolvedNode {
        double diameter;
        double footprint;
        double weight;
    };
    /// The nodes of `UnresolvedCoverAt` at `spacing`: Simpson's rule in log diameter, split where
    /// the resolution fade begins.
    std::vector<UnresolvedNode> UnresolvedNodes(double spacing) const;
    /// `UnresolvedCoverAt` with the abundance there.
    UnresolvedCover UnresolvedCoverWith(const BoulderAbundance& abundance,
                                        const std::vector<UnresolvedNode>& nodes,
                                        double spacing) const;
    /// The mean bedrock factor around (`x`, `y`) on `ground` over the cells of the density map,
    /// or squares of `BoulderBedrock::meanSize`, blended between the four nearest cell centres.
    double BedrockMean(double x, double y, const Ground& ground) const;

    /// A cell of the lattice bedrock means are taken over, on one ground.
    struct BedrockCell {
        const Ground* ground = nullptr;
        std::int64_t i = 0;
        std::int64_t j = 0;
        bool operator==(const BedrockCell&) const = default;
    };
    struct BedrockCellHash {
        std::size_t operator()(const BedrockCell& cell) const;
    };

    BoulderLayerOptions options_;
    std::shared_ptr<const CraterLayer> craters_;
    double minCraterDiameter_ = 0.0;
    double maxBackground_ = 1.0;
    double clusterMean_ = 1.0;
    double referenceCount_ = 0.0;  ///< N(>=D0) of the sizes at a size slope map's D0, per m^2.
    std::optional<BoulderMapAbundance> mapAbundance_;
    double patchMean_ = 1.0;   ///< Under a map, what the patches are divided by.
    Vec2d bedrockOrigin_{};    ///< Corner of the bedrock mean lattice, in surface units.
    Vec2d bedrockCellSize_{};  ///< Its cell sides, in surface units; signed as the map's.
    std::shared_ptr<LruCache<BedrockCell, double, BedrockCellHash>> bedrockMeans_;
    std::shared_ptr<const SizeFrequencyDistribution> candidates_;
    std::shared_ptr<const SizeFrequencyDistribution> ejectaSizes_;
    std::shared_ptr<CraterCellCache> cache_;
    std::shared_ptr<LruCache<std::uint64_t, BoulderShape>> shapes_;
    std::shared_ptr<LruCache<std::uint64_t, std::vector<BoulderFragment>>> fragments_;
    std::optional<ExposureField> exposure_;
    Surface surface_;
    std::shared_ptr<const SurfaceCells> cells_;
};

/// The height (m) of a boulder's dome of its exposed height over its diameter `distance` metres
/// from its centre, averaged over a band `width` metres wide across that distance so its rim
/// slopes over a sample instead of standing as a wall; 0 `width` samples it at the point.
double DomeHeight(const Boulder& boulder, double distance, double width = 0.0);

/// The height at surface (x, y) after a boulder's exposed part: `height` plus its dome
/// (`DomeHeight`), as `BoulderRelief::Dome` draws it.
/// @param weight Weight of the dome, in [0, 1].
/// @param width Width (m) the dome is averaged over, such as the sample spacing; 0 for none.
float AddBoulder(float height, double x, double y, const Boulder& boulder, double weight,
                 const Surface& surface = {}, double width = 0.0);

}  // namespace terraingen
