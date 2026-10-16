/// @file
/// @brief `CraterLayer`, craters generated over the grid being evaluated and applied to it.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/apply.h"
#include "terraingen/layers/craters/cell_cache.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/layer.h"

#include <cstdint>
#include <memory>

namespace terraingen {

/// @brief The models a crater layer generates and shapes craters with.
struct CraterModels {
    std::shared_ptr<const SizeFrequencyDistribution> density;  ///< Craters per m^2 by diameter.
    std::shared_ptr<const CraterAgeModel> ages;                ///< Each crater's age.
    std::shared_ptr<const DegradationModel> degradation;       ///< Each crater's state.
    std::shared_ptr<const CraterProfile> profile;              ///< Height by distance.

    /// @brief The models that shape the craters.
    /// @return The profile and degradation.
    CraterShapeModels Shape() const {
        return {profile, degradation};
    }
};

/// @brief How a `CraterLayer` generates and applies its craters.
struct CraterLayerOptions {
    /// Generate craters from the density; otherwise only the manual craters in `generation` apply.
    bool generate = true;
    std::uint32_t seed = 1;  ///< Varies the generated craters.
    /// Diameter range, spacing, manual craters and cap; `marginRadii` is set from the profile's
    /// reach.
    CraterGenerationOptions generation{.dMin = 4.0, .dMax = 250.0};
    CraterApplyOptions apply{};  ///< How the craters are shaped into the terrain.
    /// Most craters to keep generated cells of (`CraterCellCache`); 0 regenerates cells every time.
    std::size_t cacheCraters = 4000000;
    /// What the craters lie on, copied into `generation.surface` and `apply.surface`.
    Surface surface{};
};

/// @brief Craters generated over the grid being evaluated (`GenerateCraters`) and applied to it
/// (`ApplyCraters`) at the evaluation's spacing.
///
/// Generated craters are the same in a tile as in any larger grid, unless `maxCraters` caps them
/// over the whole grid; bands too small to resolve are not generated.
class CraterLayer final : public Layer {
  public:
    /// @brief Builds a crater layer.
    /// @param models The density, ages, degradation and profile; none may be null.
    /// @param options How craters are generated and applied.
    /// @throws std::invalid_argument for a missing model or invalid options.
    CraterLayer(CraterModels models, CraterLayerOptions options);

    /// @brief Generates and applies the craters reaching the grid; datums are fitted to
    /// `context.ground` where given, otherwise to `heights` as passed in.
    /// @param heights The heights to change in place.
    /// @param context How the layer is evaluated.
    /// @param features Receives the craters centred in the grid, with their attributes.
    /// @throws std::domain_error as `DegradationModel::State`.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// @brief The largest crater the layer places.
    /// @return `dMax` (the density's largest valid diameter when unbounded) or the largest manual
    /// crater, in metres.
    double MaxFeatureDiameter() const override;

    /// @brief The craters reaching a point applied to `height`, bit-identical to a cell centred
    /// there; datums are fitted to `context.ground`.
    /// @param height The height before the layer; NaN stays NaN.
    /// @param x Surface x of the point.
    /// @param y Surface y of the point.
    /// @param context How the layer is evaluated.
    /// @return The height after the layer.
    /// @throws std::invalid_argument as `ValidatePointContext`, if the layer has a datum and
    /// `context.ground` is null, or if the layer has `maxCraters` (which depends on the whole
    /// grid).
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    /// @brief The craters this layer applies to a grid covering a region: those generated, and the
    /// manual ones, centred in it or within the profile's reach of it.
    /// @param region The region, in surface coordinates.
    /// @param spacing The sample spacing; bands too small to resolve at it are skipped
    /// (`CraterGenerationOptions::minResolvedDiameter`). 0 generates every band.
    /// @return The craters.
    CraterList Craters(const Bounds2d& region, double spacing = 0.0) const;

    /// @brief A crater's attributes in the state the degradation model gives it.
    /// @param crater The crater.
    /// @return Its depth, rim height and freshness.
    /// @throws std::domain_error as `DegradationModel::State`.
    CraterAttributes Attributes(const Crater& crater) const;

    /// @brief The cell cache.
    /// @return The cache, or null when `cacheCraters` is 0.
    const CraterCellCache* Cache() const {
        return cache_.get();
    }

    /// @brief The models.
    /// @return The models the layer was made with.
    const CraterModels& Models() const {
        return models_;
    }

    /// @brief The layer options.
    /// @return The options, with surfaces and `marginRadii` filled in.
    const CraterLayerOptions& Options() const {
        return options_;
    }

  private:
    CraterModels models_;
    CraterLayerOptions options_;
    std::shared_ptr<const SurfaceCells> cells_;
    std::shared_ptr<CraterCellCache> cache_;
};

}  // namespace terraingen
