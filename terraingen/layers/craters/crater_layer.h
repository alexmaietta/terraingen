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
};

/// Settings for a `CraterLayer`.
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
};

/// Craters generated over the grid being evaluated and applied to it: `GenerateCraters` with
/// `PlanarCells`, then `ApplyCraters` at the evaluation's sample spacing. Generated craters are the
/// same in a tile as in any larger grid containing it, unless `maxCraters` caps them over the whole
/// grid. Cells are kept in a `CraterCellCache`, and bands too small to resolve at the spacing are
/// not generated at all.
class CraterLayer final : public Layer {
  public:
    /// @throws std::invalid_argument for a missing model or invalid options.
    CraterLayer(CraterModels models, CraterLayerOptions options);

    void Apply(Raster<float>& heights, double spacing, TerrainFeatures& features) const override;

    /// The craters reaching (`x`, `y`) added to `height` there, in the order `ApplyCraters` applies
    /// them, to the same bits as a cell centred there; NaN stays NaN.
    /// @throws std::invalid_argument if `spacing` is not positive and finite, or the layer has a
    /// datum (which needs the heights around the point) or `maxCraters` (which depends on the whole
    /// grid).
    float ApplyAt(float height, double x, double y, double spacing) const override;

    /// The craters this layer applies to a grid covering `region`: those generated, and the manual
    /// ones, centred in it or within the profile's reach of it. With a positive `spacing`, bands
    /// too small to resolve at it are skipped (`CraterGenerationOptions::minResolvedDiameter`).
    CraterList Craters(const Bounds2d& region, double spacing = 0.0) const;

    /// The cell cache, or null when `cacheCraters` is 0.
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
    std::shared_ptr<CraterCellCache> cache_;
};

}  // namespace terraingen
