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

/// The models a crater layer generates and shapes craters with.
struct CraterModels {
    std::shared_ptr<const SizeFrequencyDistribution> density;
    std::shared_ptr<const CraterAgeModel> ages;
    std::shared_ptr<const DegradationModel> degradation;
    std::shared_ptr<const CraterProfile> profile;

    /// @brief The models that shape the craters.
    /// @return The profile and degradation.
    CraterShapeModels Shape() const {
        return {profile, degradation};
    }
};

/// How a `CraterLayer` generates and applies its craters.
struct CraterLayerOptions {
    /// Generate craters from the density; otherwise only the manual craters in `generation` apply.
    bool generate = true;
    std::uint32_t seed = 1;
    /// Diameter range, spacing, manual craters and cap; `marginRadii` is set from the profile's
    /// reach, so craters centred outside a grid but reaching into it are applied.
    CraterGenerationOptions generation{.dMin = 4.0, .dMax = 250.0};
    CraterApplyOptions apply{};
    /// Most craters to keep generated cells of (`CraterCellCache`); 0 regenerates cells every time.
    std::size_t cacheCraters = 4000000;
    /// What the craters lie on, copied into `generation.surface` and `apply.surface`: on a sphere
    /// they are generated in `CubeSphereCells` and placed by longitude and latitude.
    Surface surface{};
};

/// Craters generated over the grid being evaluated and applied to it: `GenerateCraters`, then
/// `ApplyCraters` at the evaluation's sample spacing. Generated craters are the same in a tile as
/// in any larger grid containing it, unless `maxCraters` caps them over the whole grid. Cells are
/// kept in a `CraterCellCache`, and bands too small to resolve at the spacing are not generated.
class CraterLayer final : public Layer {
  public:
    /// @brief Builds a crater layer.
    /// @param models The density, ages, degradation and profile; none may be null.
    /// @param options How craters are generated and applied.
    /// @throws std::invalid_argument for a missing model or invalid options.
    CraterLayer(CraterModels models, CraterLayerOptions options);

    /// Craters with a datum fit it to `context.ground` where given, otherwise to `heights` as
    /// passed in.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// @brief The largest crater the layer places.
    /// @return `dMax` (the density's largest valid diameter when unbounded) or the largest manual
    /// crater, in metres.
    double MaxFeatureDiameter() const override;

    /// The craters reaching (`x`, `y`) added to `height` there, in the order `ApplyCraters` applies
    /// them, to the same bits as a cell centred there; NaN stays NaN. Datums are fitted to
    /// `context.ground`.
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
    CraterAttributes Attributes(const Crater& crater) const;

    /// @brief The cell cache.
    /// @return The cache, or null when `cacheCraters` is 0.
    const CraterCellCache* Cache() const {
        return cache_.get();
    }

    const CraterModels& Models() const {
        return models_;
    }

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
