#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/apply.h"
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
};

/// Craters generated over the grid being evaluated and applied to it: `GenerateCraters` with
/// `PlanarCells`, then `ApplyCraters`. Generated craters are the same in a tile as in any larger
/// grid containing it, unless `maxCraters` caps them over the whole grid.
///
/// A sample spacing coarser than the grid's cells raises `minDiameterPixels` accordingly, so
/// craters too small to resolve at the spacing are left out.
class CraterLayer final : public Layer {
  public:
    /// @throws std::invalid_argument for a missing model or invalid options.
    CraterLayer(CraterModels models, CraterLayerOptions options);

    void Apply(Raster<float>& heights, double spacing, TerrainFeatures& features) const override;

    /// The craters this layer applies to a grid covering `region`: those generated, and the manual
    /// ones, centred in it or within the profile's reach of it.
    CraterList Craters(const Bounds2d& region) const;

    const CraterModels& Models() const {
        return models_;
    }

    const CraterLayerOptions& Options() const {
        return options_;
    }

  private:
    CraterModels models_;
    CraterLayerOptions options_;
};

}  // namespace terraingen
