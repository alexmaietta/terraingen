// SPDX-License-Identifier: BSD-3-Clause

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
#include "terraingen/random/stream.h"
#include "terraingen/sources/source.h"
#include "terraingen/util/lru_cache.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
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

    bool operator==(const BoulderAbundance& other) const {
        return background == other.background && ejecta == other.ejecta &&
               ejectaWear == other.ejectaWear && exponent == other.exponent;
    }
};

/// The share of a sample's area covered by boulders too small to resolve in it.
struct UnresolvedCover {
    double mean = 0.0;      ///< Expected share.
    double variance = 0.0;  ///< Variance of the share from the boulders' Poisson scatter.

    bool operator==(const UnresolvedCover& other) const {
        return mean == other.mean && variance == other.variance;
    }
};

/// Boulders scattered over the ground: between craters, in patches and richer on bedrock,
/// plus blocks around fresh craters (which need `craters`).
///
/// Candidates are drawn per cell in size bands and thinned by a uniform keyed by each ID, so
/// boulders depend only on position and agree on every grid, tile and point. Without
/// `generateRandom` only the manual boulders are placed and no candidates are drawn.
class BoulderLayer final : public Layer, public BoulderPlacer, public MaterialSource {
  public:
    /// A layer of only `boulders`, with `generateRandom` off: no random boulders or crater blocks.
    /// @param options How the boulders are posed and drawn, such as `rendering`, `pose` and
    /// `fillet`; its `manual` boulders are replaced by `boulders`.
    /// @param surface What the boulders lie on.
    /// @throws std::invalid_argument as the constructor.
    static std::shared_ptr<BoulderLayer> Manual(std::vector<ManualBoulder> boulders,
                                                BoulderLayerOptions options = {},
                                                Surface surface = {});

    /// On a sphere, candidates are drawn in `CubeSphereCells`, distances are great-circle, and
    /// relief and model placements are in each boulder's local east, north and up frame, its
    /// origin on the datum below the boulder.
    /// @param craters The crater layer whose craters throw blocks; may be null without ejecta or
    /// without `generateRandom`.
    /// @param surface The crater layer's surface.
    /// @param source The source the terrain's heights come from, which with
    /// `options.sourceResolvesPixels` holds the boulders it resolves; null for none.
    /// @throws std::invalid_argument for invalid options, ejecta of random boulders without
    /// `craters`, or a crater layer on another surface.
    BoulderLayer(BoulderLayerOptions options, std::shared_ptr<const CraterLayer> craters = nullptr,
                 Surface surface = {}, std::shared_ptr<const Source> source = nullptr);

    /// Boulders whose ejecta come from every one of `craters`, each crater's blocks worn by its
    /// own layer's degradation; overlapping ejecta take the largest share as within one layer.
    /// @throws std::invalid_argument as the single-layer constructor, or for a null layer.
    BoulderLayer(BoulderLayerOptions options,
                 std::vector<std::shared_ptr<const CraterLayer>> craters, Surface surface,
                 std::shared_ptr<const Source> source = nullptr);

    /// The boulders centered in `region` (surface coordinates) or reaching it with their radius or
    /// fillet, in a fixed order.
    /// @param spacing Sample spacing in meters; boulders narrower than `fade.startSamples` samples
    /// at it, which the heights leave out, are left out and models are marked at it; 0 keeps
    /// every boulder.
    /// @param ground Read under each boulder; needed with the exposure field.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    std::vector<Boulder> Boulders(const Bounds2d& region, double spacing = 0.0,
                                  const Ground* ground = nullptr) const;

    /// The background and ejecta abundance at surface (x, y); both 0 without `generateRandom`.
    /// @param ground Needed with the exposure field.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    BoulderAbundance AbundanceAt(double x, double y, const Ground* ground = nullptr) const;

    /// The share of a sample `spacing` meters across at surface (x, y) covered by boulders too
    /// small to resolve (weighted by one minus `ResolutionWeight`) and its Poisson variance over
    /// the sample, in visible plan area; none without `generateRandom`.
    /// @param spacing Positive.
    /// @param ground Needed with the exposure field.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    UnresolvedCover UnresolvedCoverAt(double x, double y, double spacing,
                                      const Ground* ground = nullptr) const;
    /// `UnresolvedCoverAt` at every cell center of `grid`, row-major, the ejecta craters found
    /// once.
    /// @throws std::invalid_argument as `UnresolvedCoverAt`.
    std::vector<UnresolvedCover> UnresolvedCoverOver(const RasterGrid& grid, double spacing,
                                                     const Ground* ground, unsigned threads) const;

    /// The largest boulder (m): `sizes.maxDiameter` with `generateRandom`, or the largest manual
    /// boulder when larger.
    double MaxFeatureDiameter() const;

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
    /// The purposes a boulder draws numbers for, each its own substream of its ID.
    enum class BoulderDraw : std::uint32_t { Keep, Pose, Wear, Settle, Abrade, Fragments };
    /// The substream of `kBoulderAttributeStream` for boulder or shape `id` and purpose `draw`.
    RandomStream AttributeStream(std::uint64_t id, BoulderDraw draw) const;
    /// Builds the background abundance models, the candidate sizes and the cluster mean.
    void SetUpAbundance();
    /// Sets the lattice bedrock means are taken over and their cache.
    void SetUpBedrockMeans();
    /// Makes the caches of shared shapes and their fragments.
    void SetUpShapeCaches();
    /// The boulders centered in `region` or reaching it, as `Boulders`, leaving out those and the
    /// pieces narrower than `smallest` meters (0 keeps every one) and marking models at `spacing`.
    std::vector<Boulder> PlaceBoulders(const Bounds2d& region, double spacing, double smallest,
                                       const Ground* ground) const;
    /// The candidates of `region` at least `smallest` meters across (0 for all), before thinning;
    /// only the manual boulders reaching it without `generateRandom`.
    CraterList ScatterBoulderCandidates(const Bounds2d& region, double smallest) const;
    /// A candidate thinning keeps, with what it was kept by.
    struct ThinnedCandidate {
        Crater candidate;
        BoulderAbundance abundance;
        double density = 0.0;
    };
    /// `candidate` with its abundance and density, or nothing when thinning drops it.
    std::optional<ThinnedCandidate> Thin(const Crater& candidate, const Ground* ground,
                                         const EjectaIndex& ejecta) const;
    /// The boulder a thinned candidate becomes before it meets the ground: its size, burial,
    /// yaw, lean and wear drawn by its ID, then those `fixed` sets (null for none) and its shape
    /// seed, and whether it is a model at `spacing`.
    Boulder DrawPose(const ThinnedCandidate& thinned, double spacing,
                     const ManualBoulder* fixed) const;
    /// The manual boulder of ID `id` that fixes any of its fields, or null.
    const ManualBoulder* Fixed(std::uint64_t id) const;
    /// Whether `boulder`'s model is tipped over to rest stably: with `pose.settle`, unless it is
    /// a manual boulder that fixes its orientation.
    bool Settles(const Boulder& boulder) const;
    /// A thinned candidate as a boulder on `ground`, or nothing when it is left out on steep
    /// ground.
    std::optional<Boulder> PlaceCandidate(const ThinnedCandidate& thinned, double spacing,
                                          const Ground* ground) const;
    /// Sets `boulder`'s ground height from `ground` under its center; unset without a ground or
    /// where the ground is missing.
    void ReadGround(Boulder& boulder, const Ground* ground) const;
    /// The boulders `candidates` give, in their order: those thinning keeps, their ground read in
    /// one pass (`Ground::HeightsAt`) and then placed.
    std::vector<Boulder> DrawBoulders(const CraterList& candidates, double spacing,
                                      const Ground* ground, const EjectaIndex& ejecta) const;
    /// Margin in radii a boulder's own relief and fillet reach beyond its center.
    double OwnMarginRadii() const;
    /// Margin in radii its pieces may scatter beyond that when boulders break.
    double BreakMarginRadii() const;
    /// Sets `boulder`'s resting axis and tilt from the pose options, or those `fixed` sets (null
    /// for none), on the slope of `ground` when they follow it; a fixed tilt and azimuth come out
    /// exactly off a followed slope.
    void Settle(Boulder& boulder, const Ground* ground, const ManualBoulder* fixed) const;
    /// The up axis drawn from `random` by the pose's tilt, or set by `fixedTilt`'s tilt and
    /// azimuth when not null.
    Vec3d UpAxis(RandomStream& random, const ManualBoulder* fixedTilt) const;
    /// The rise east and north (m/m) of `ground` across `boulder`'s diameter; none where the
    /// ground is missing.
    std::optional<Vec2d> Gradient(const Boulder& boulder, const Ground& ground) const;
    /// Rolls `boulder` down ground steeper than `maxSlope` until it rests, as `rollDistance`
    /// allows; false when it would roll farther, or with no roll distance on steep ground.
    bool Roll(Boulder& boulder, const Ground& ground) const;
    /// The pieces `parent` breaks into at its wear, those at least `smallest` meters across, models
    /// marked at `spacing` and their ground left to `ReadGrounds`; `parent` alone when it is too
    /// small or too fresh to break, or a manual boulder fixing any field.
    std::vector<Boulder> Break(const Boulder& parent, double spacing, double smallest) const;
    /// Sets the ground of the boulders `indices` of `boulders` from `ground`, read in one pass;
    /// unset without a ground or where it is missing.
    void ReadGrounds(std::vector<Boulder>& boulders, const std::vector<std::size_t>& indices,
                     const Ground* ground) const;
    /// How far a breaking boulder has come apart: its cracks' opening, 0 to 1 from `splitWear`
    /// to `rubbleWear`, and its pieces' scatter, 0 to 1 beyond.
    struct BreakStage {
        double opening = 0.0;
        double scatter = 0.0;
    };
    /// The stage `parent` has reached at its wear.
    BreakStage Stage(const Boulder& parent) const;
    /// What every piece of a breaking boulder shares: its parent's shape placed at the origin on
    /// level ground, its stage and the shape's short axis.
    struct PieceFrame {
        BoulderPlacement placement;
        BreakStage stage;
        double shortAxis = 1.0;
    };
    /// Piece `k`, `fragment`, of `parent`, moved out by `frame`'s stage with the uniforms `push`
    /// and `twist`; its model mark and ground are left to the caller.
    Boulder PlacePiece(const Boulder& parent, const BoulderFragment& fragment, std::size_t k,
                       const PieceFrame& frame, double push, double twist) const;
    /// The pieces of the shape of seed `shapeSeed`, shared by every boulder of that shape.
    std::shared_ptr<const std::vector<BoulderFragment>>
    CachedFragments(std::uint64_t shapeSeed) const;
    /// The shape of seed `shapeSeed`.
    std::shared_ptr<const BoulderShape> CachedShape(std::uint64_t shapeSeed) const;
    /// The wear (0 fresh to 1 worn) of a boulder of `diameter` keyed by `id`: that of the crater
    /// its block came from, drawn by each source's share of `density`, or uniform between craters.
    double Wear(double diameter, const BoulderAbundance& abundance, double density,
                std::uint64_t id) const;
    /// `height` at (`x`, `y`) raised to at least `base` (the height before any boulder) plus the
    /// boulder's exposed part and its fillet, so boulders and fillets form a union instead of
    /// stacking; `placed` is its shape placed on level ground at 0 for `BoulderRelief::Shape`, a
    /// dome is averaged over `width` meters, and without `rock` only the fillet is drawn.
    float AddRelief(float height, float base, double x, double y, const Boulder& boulder,
                    const TriangleMesh* placed, double weight, bool rock, double width) const;
    /// The height of `boulder`'s exposed rock above the ground at (`x`, `y`), 0 off it; `placed`
    /// and `width` as in `AddRelief`.
    double RockRelief(double x, double y, const Boulder& boulder, const TriangleMesh* placed,
                      double width) const;
    /// `boulder` in its local frame: centered at the origin on a sphere, unchanged on a plane.
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

        /// The placed shape, or null for a dome.
        const TriangleMesh* Shape() const {
            return placed ? &*placed : nullptr;
        }
    };
    /// The weight of `relief` where its drawn rock stands at `p`, else 0.
    double RockCover(const Relief& relief, const Vec2d& p) const;
    /// The weight at `spacing` of `boulder` where it is a model over `p`, else 0.
    double ModelCover(const Boulder& boulder, const Vec2d& p, double spacing) const;
    /// The boulders whose relief at `spacing` can reach `region`.
    std::vector<Boulder> ReliefBoulders(const Bounds2d& region, double spacing,
                                        const Ground* ground) const;
    /// The boxes of `reliefs`, in order.
    static std::vector<Bounds2d> Boxes(const std::vector<Relief>& reliefs);
    /// The boulders of `boulders` resolved at `spacing` with relief or a fillet to draw, in order.
    std::vector<Relief> PrepareRelief(const std::vector<Boulder>& boulders, double spacing) const;

    /// The ejecta craters that can add blocks in `region`, indexed by how far their blocks reach.
    EjectaIndex EjectaCraters(const Bounds2d& region) const;
    /// The abundance at (`x`, `y`) with the ejecta craters `ejecta` around.
    BoulderAbundance Abundance(double x, double y, const Ground* ground,
                               const EjectaIndex& ejecta) const;
    /// The distance (m) from surface (x, y) to the nearest cluster center.
    double NearestClusterDistance(double x, double y) const;
    /// The sum at surface (x, y) of the Gaussians of the `Thomas` cluster centers around it.
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
    /// Whether boulders too small to resolve at `spacing` are left; throws for a `spacing` that is
    /// not positive.
    bool LeavesUnresolved(double spacing) const;
    /// `UnresolvedCoverAt` with the abundance there.
    UnresolvedCover UnresolvedCoverWith(const BoulderAbundance& abundance,
                                        const std::vector<UnresolvedNode>& nodes,
                                        double spacing) const;
    /// The mean bedrock factor around (`x`, `y`) on `ground` over the cells of the density map,
    /// or squares of `BoulderBedrock::meanSize`, blended between the four nearest cell centers.
    double BedrockMean(double x, double y, const Ground& ground) const;

    /// A cell of the lattice bedrock means are taken over, on one ground.
    struct BedrockCell {
        const Ground* ground = nullptr;
        std::int64_t i = 0;
        std::int64_t j = 0;
        bool operator==(const BedrockCell& other) const {
            return ground == other.ground && i == other.i && j == other.j;
        }
    };
    struct BedrockCellHash {
        std::size_t operator()(const BedrockCell& cell) const;
    };

    BoulderLayerOptions options_;
    std::vector<std::shared_ptr<const CraterLayer>> craters_;
    double minCraterDiameter_ = 0.0;  ///< Smallest crater throwing blocks (m); 0 for none.
    double largestManual_ = 0.0;      ///< Largest manual boulder (m); 0 for none.
    double clusterMean_ = 1.0;
    std::uint32_t clusterSeed_ = 0;  ///< Seed of the cluster centers' Worley noise.
    std::uint32_t patchSeed_ = 0;    ///< Seed of the abundance patch noise.
    double referenceCount_ = 0.0;    ///< N(>=D0) of the sizes at a size slope map's D0, per m^2.
    std::optional<BoulderBackground> background_;  ///< Set with `generateRandom`.
    Vec2d bedrockOrigin_{};    ///< Corner of the bedrock mean lattice, in surface units.
    Vec2d bedrockCellSize_{};  ///< Its cell sides, in surface units; signed as the map's.
    std::shared_ptr<LruCache<BedrockCell, double, BedrockCellHash>> bedrockMeans_;
    std::shared_ptr<const SizeFrequencyDistribution> candidates_;
    std::shared_ptr<const SizeFrequencyDistribution> ejectaSizes_;
    std::shared_ptr<CraterCellCache> cache_;
    std::shared_ptr<LruCache<std::uint64_t, BoulderShape>> shapes_;
    std::shared_ptr<LruCache<std::uint64_t, std::vector<BoulderFragment>>> fragments_;
    LayerExposure exposure_;
    /// The manual boulders that fix any field, by the ID they are placed with.
    std::unordered_map<std::uint64_t, ManualBoulder> fixed_;
    Surface surface_;
    std::shared_ptr<const SurfaceCells> cells_;
};

}  // namespace terraingen
