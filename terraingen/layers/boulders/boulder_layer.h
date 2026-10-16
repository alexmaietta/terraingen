/// @file
/// @brief `BoulderLayer`, boulders scattered over the ground and around fresh craters.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/boulders/boulder_fragments.h"
#include "terraingen/layers/boulders/boulder_models.h"
#include "terraingen/layers/boulders/boulder_options.h"
#include "terraingen/layers/boulders/boulder_placer.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/generation/cell_cache.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/layer.h"
#include "terraingen/util/lru_cache.h"

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

    bool operator==(const BoulderAbundance&) const = default;
};

/// Boulders scattered over the ground: between craters, in patches and richer on bedrock,
/// plus blocks around fresh craters (which need `craters`).
///
/// Candidates are drawn per cell in size bands and thinned by a uniform keyed by each ID, so
/// boulders depend only on position and agree on every grid, tile and point.
class BoulderLayer final : public Layer, public BoulderPlacer {
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

  protected:
    /// Draws the exposed parts of non-model boulders into the samples.
    /// @throws std::invalid_argument if the exposure field is enabled and `context.ground` is null.
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    /// The candidates of `region`, before thinning.
    CraterList ScatterBoulderCandidates(const Bounds2d& region, double spacing) const;
    /// `candidate` as a boulder, or nothing when thinning drops it.
    std::optional<Boulder> DrawBoulder(const Crater& candidate, double spacing,
                                       const Ground* ground, const CraterList& ejecta) const;
    /// Whether boulders are drawn as the union of their solids and fillets over the ground before
    /// any of them (with fillets or fragmentation) rather than added on one another.
    bool Unites() const;
    /// Margin in radii a boulder's own relief and fillet reach beyond its centre.
    double OwnMarginRadii() const;
    /// Margin in radii its pieces may scatter beyond that when boulders break.
    double BreakMarginRadii() const;
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
    /// `height` at (`x`, `y`) with the boulder's exposed part added and then raised to at least
    /// `base` (the height before any boulder) plus its fillet, so fillets never stack on rock or on
    /// each other; `placed` is its shape placed on level ground at 0 for `BoulderRelief::Shape`,
    /// and without `rock` only the fillet is drawn.
    float AddRelief(float height, float base, double x, double y, const Boulder& boulder,
                    const TriangleMesh* placed, double weight, bool rock) const;
    /// `boulder` in its local frame: centred at the origin on a sphere, unchanged on a plane.
    Boulder InLocalFrame(const Boulder& boulder) const;
    /// `boulder`'s shape placed on level ground at 0.
    TriangleMesh PlacedOnLevel(const Boulder& boulder) const;
    /// A boulder drawn into the heights: its weight at the spacing, whether its rock is drawn or
    /// only its fillet, its shape placed on level ground (for `BoulderRelief::Shape`) and the box
    /// its relief covers.
    struct Relief {
        const Boulder* boulder;
        double weight;
        bool rock;
        std::optional<TriangleMesh> placed;
        Bounds2d box;
    };
    /// The boulders of `boulders` resolved at `spacing` with relief or a fillet to draw, in order.
    std::vector<Relief> PrepareRelief(const std::vector<Boulder>& boulders, double spacing) const;
    /// Draws `reliefs` into every sample their boxes hold.
    void DrawRelief(SampleBlock& samples, const std::vector<Relief>& reliefs,
                    unsigned threads) const;

    /// The ejecta craters that can add blocks in `region`.
    CraterList EjectaCraters(const Bounds2d& region) const;
    /// The abundance at (`x`, `y`) with the ejecta craters `ejecta` around.
    BoulderAbundance Abundance(double x, double y, const Ground* ground,
                               const CraterList& ejecta) const;
    /// The distance (m) from surface (x, y) to the nearest cluster centre.
    double NearestClusterDistance(double x, double y) const;
    double BackgroundAbundance(double x, double y, const Ground* ground) const;
    /// Sets `abundance`'s ejecta share and wear at (`x`, `y`) from the craters `ejecta`.
    void EjectaAbundance(double x, double y, const CraterList& ejecta,
                         BoulderAbundance& abundance) const;
    double Density(double diameter, const BoulderAbundance& abundance) const;

    BoulderLayerOptions options_;
    std::shared_ptr<const CraterLayer> craters_;
    double minCraterDiameter_ = 0.0;
    double maxBackground_ = 1.0;
    double clusterMean_ = 1.0;
    std::shared_ptr<const SizeFrequencyDistribution> candidates_;
    std::shared_ptr<const SizeFrequencyDistribution> ejectaSizes_;
    std::shared_ptr<CraterCellCache> cache_;
    std::shared_ptr<LruCache<std::uint64_t, BoulderShape>> shapes_;
    std::shared_ptr<LruCache<std::uint64_t, std::vector<BoulderFragment>>> fragments_;
    std::optional<ExposureField> exposure_;
    Surface surface_;
    std::shared_ptr<const SurfaceCells> cells_;
};

/// The height at surface (x, y) after a boulder's exposed part: a dome of its exposed height over
/// its diameter, as `BoulderRelief::Dome` draws it.
/// @param weight Weight of the dome, in [0, 1].
float AddBoulder(float height, double x, double y, const Boulder& boulder, double weight,
                 const Surface& surface = {});

}  // namespace terraingen
