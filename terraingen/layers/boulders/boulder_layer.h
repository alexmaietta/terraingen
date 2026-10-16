/// @file
/// @brief `BoulderLayer`, boulders scattered over the ground and around fresh craters.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/boulders/boulder_meshes.h"
#include "terraingen/layers/boulders/boulder_options.h"
#include "terraingen/layers/craters/cell_cache.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/layer.h"
#include "terraingen/util/lru_cache.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// @brief How rich the boulders are at a point.
struct BoulderAbundance {
    /// Density of the boulders between craters over its mean, from patches and bedrock.
    double background = 1.0;
    /// Share of fresh crater blocks, from the profile and age of the craters around.
    double ejecta = 0.0;

    bool operator==(const BoulderAbundance&) const = default;
};

/// @brief Boulders scattered over the ground: between craters, in patches and richer on bedrock,
/// plus blocks around fresh craters (which need `craters`).
///
/// Candidates are drawn per cell in size bands and thinned by a uniform keyed by each ID, so
/// boulders depend only on position and agree on every grid, tile and point.
class BoulderLayer final : public Layer {
  public:
    /// @brief Builds a boulder layer.
    ///
    /// On a sphere, candidates are drawn in `CubeSphereCells`, distances are great-circle, and
    /// relief and model placements are in each boulder's local east, north and up frame, its
    /// origin on the datum below the boulder.
    /// @param options How boulders are sized, placed, posed and drawn.
    /// @param craters The crater layer whose craters throw blocks; may be null without ejecta.
    /// @param surface What the boulders lie on; the crater layer's surface.
    /// @throws std::invalid_argument for invalid options, ejecta without `craters`, or a crater
    /// layer on another surface.
    BoulderLayer(BoulderLayerOptions options, std::shared_ptr<const CraterLayer> craters = nullptr,
                 Surface surface = {});

    /// @brief The boulders of a region.
    /// @param region The region, in surface coordinates.
    /// @param spacing The sample spacing; boulders too small to resolve at it are left out, and
    /// models are marked at it. 0 for every boulder.
    /// @param ground The ground, read under each boulder; needed with the exposure field.
    /// @return The boulders centred in `region` or within their radius of it, in a fixed order.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    std::vector<Boulder> Boulders(const Bounds2d& region, double spacing = 0.0,
                                  const Ground* ground = nullptr) const;

    /// @brief How rich the boulders are at a point.
    /// @param x Surface x of the point.
    /// @param y Surface y of the point.
    /// @param ground The ground; needed with the exposure field.
    /// @return The background and ejecta abundance.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    BoulderAbundance AbundanceAt(double x, double y, const Ground* ground = nullptr) const;

    /// @brief The density of the boulders of a size at a point.
    /// @param diameter The boulders' diameter, within the layer's sizes, in metres.
    /// @param x Surface x of the point.
    /// @param y Surface y of the point.
    /// @param ground The ground; needed with the exposure field.
    /// @return -dN/dD, per m^2 per metre of diameter.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    double DensityAt(double diameter, double x, double y, const Ground* ground = nullptr) const;

    /// @brief The largest boulder.
    /// @return `sizes.maxDiameter`, in metres.
    double MaxFeatureDiameter() const override {
        return options_.sizes.maxDiameter;
    }

    /// @brief The layer options.
    /// @return The options the layer was made with.
    const BoulderLayerOptions& Options() const {
        return options_;
    }

    /// @brief The 3D shape of a boulder, shared between boulders with the same shape seed.
    /// @param boulder The boulder.
    /// @return Its shape.
    std::shared_ptr<const BoulderShape> Shape(const Boulder& boulder) const;

  protected:
    /// Draws the exposed parts of non-model boulders into the samples. On a grid it records the
    /// boulders centred in or within a radius of it, and places models on the ground read at their
    /// centres: from `context.ground`, otherwise the heights as given.
    /// @throws std::invalid_argument if the exposure field is enabled and `context.ground` is null.
    void ApplySamples(SampleBlock& samples, const LayerContext& context,
                      TerrainFeatures& features) const override;

  private:
    void ApplyToGrid(Raster<float>& heights, const LayerContext& context,
                     TerrainFeatures& features) const;
    float HeightAtPoint(float height, double x, double y, const LayerContext& context) const;
    /// The candidates of `region`, before thinning.
    CraterList ScatterBoulderCandidates(const Bounds2d& region, double spacing) const;
    /// `candidate` as a boulder, or nothing when thinning drops it.
    std::optional<Boulder> DrawBoulder(const Crater& candidate, double spacing,
                                       const Ground* ground, const CraterList& ejecta) const;
    /// The boulder's exposed part added to `height` at (`x`, `y`), with `placed` its shape placed
    /// on level ground at 0 for `BoulderRelief::Shape`.
    float AddRelief(float height, double x, double y, const Boulder& boulder,
                    const TriangleMesh* placed, double weight) const;
    /// `boulder` in its local frame: centred at the origin on a sphere, unchanged on a plane.
    Boulder InLocalFrame(const Boulder& boulder) const;
    /// `boulder`'s shape placed on level ground at 0.
    TriangleMesh PlacedOnLevel(const Boulder& boulder) const;
    /// Draws the relief of `boulders` into `heights`.
    void DrawRelief(Raster<float>& heights, const std::vector<Boulder>& boulders, double spacing,
                    unsigned threads) const;

    /// The ejecta craters that can add blocks in `region`.
    CraterList EjectaCraters(const Bounds2d& region) const;
    /// The abundance at (`x`, `y`) with the ejecta craters `ejecta` around.
    BoulderAbundance Abundance(double x, double y, const Ground* ground,
                               const CraterList& ejecta) const;
    double BackgroundAbundance(double x, double y, const Ground* ground) const;
    double EjectaAbundance(double x, double y, const CraterList& ejecta) const;
    double Density(double diameter, const BoulderAbundance& abundance) const;

    BoulderLayerOptions options_;
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

/// @brief The height at a point after a boulder's exposed part: a dome of its exposed height over
/// its diameter, as `BoulderRelief::Dome` draws it.
/// @param height The height before the boulder.
/// @param x Surface x of the point.
/// @param y Surface y of the point.
/// @param boulder The boulder.
/// @param weight Weight of the dome, in [0, 1].
/// @param surface What the boulder lies on.
/// @return The height after the boulder.
float AddBoulder(float height, double x, double y, const Boulder& boulder, double weight,
                 const Surface& surface = {});

}  // namespace terraingen
