// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `CraterLayer`, craters generated over the grid being evaluated and applied to it.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/crater_placer.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/generation/cell_cache.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/craters/secondary_model.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/craters/shaping/attributes.h"
#include "terraingen/layers/density_map.h"
#include "terraingen/layers/layer.h"
#include "terraingen/sources/source.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// The models a crater layer generates and shapes craters with.
struct CraterModels {
    std::shared_ptr<const SizeFrequencyDistribution> density;  ///< Craters per m^2 by diameter.
    std::shared_ptr<const CraterAgeModel> ages;                ///< Each crater's age.
    std::shared_ptr<const DegradationModel> degradation;       ///< Each crater's state.
    std::shared_ptr<const CraterProfile> profile;              ///< Height by distance.

    /// The profile and degradation, the models that shape the craters.
    CraterShapeModels Shape() const {
        return {profile, degradation};
    }
};

/// How a `CraterLayer` generates and applies its craters.
struct CraterLayerOptions {
    /// Draws random craters from the density; off places only the manual craters in
    /// `generation`, which are placed either way.
    bool generateRandom = true;
    std::uint32_t seed = 1;  ///< Varies the generated craters.
    /// Diameter range, spacing, manual craters and cap; `marginRadii` is set from the profile's
    /// reach.
    CraterGenerationOptions generation = [] {
        CraterGenerationOptions options;
        options.dMin = 4.0;
        options.dMax = 250.0;
        return options;
    }();
    CraterApplyOptions apply{};  ///< How the craters are shaped into the terrain.
    /// Most craters to keep generated cells of (`CraterCellCache`); 0 regenerates cells every time.
    std::size_t cacheCraters = 4000000;
    /// What the craters lie on, copied into `generation.surface` and `apply.surface`.
    Surface surface{};
    /// A map that sets the density point by point (`DensityMap`); unset keeps the density the
    /// same everywhere. Manual craters are not thinned by it; with a catalog among them, set
    /// `generation.completeness` so generated craters do not repeat it.
    std::optional<DensityMap> densityMap{};
    /// The secondaries `SecondaryCraterLayer`s throw from these craters. A `total` count or areal
    /// fraction map counts them too, so craters too small to throw secondaries the map measures
    /// are drawn at the map less those secondaries' density there; larger craters, which throw
    /// them, follow the map alone; with spacing they also take precedence over smaller craters, so
    /// the throwers never depend on what they throw at. Not with `generation.maxCraters`.
    std::vector<SecondaryCraterOptions> secondaries{};
    /// Source cells across from which the source's measured data already holds a crater: with a
    /// source given to the layer, generated craters at least that many of its cells across are
    /// dropped (`SourceResolvedLimit`), the minimum with `generation.completeness` when both are
    /// set. Unset, or without a source, keeps every generated crater.
    std::optional<double> sourceResolvesPixels = 4.0;
};

/// Craters generated over the grid being evaluated (`GenerateCraters`) and applied to it
/// (`ApplyCraters`) at the evaluation's spacing.
///
/// Generated craters are the same in a tile as in any larger grid, unless `maxCraters` caps them
/// over the whole grid; bands too small to resolve are not generated.
class CraterLayer final : public Layer, public CraterPlacer {
  public:
    /// @param models None may be null.
    /// @param source The source the terrain's heights come from, which with
    /// `options.sourceResolvesPixels` holds the craters it resolves; null for none.
    /// @throws std::invalid_argument for a missing model or invalid options.
    CraterLayer(CraterModels models, CraterLayerOptions options,
                std::shared_ptr<const Source> source = nullptr);

    /// A layer of only `craters`, shaped by `models` and `apply`: `generateRandom` off, so no
    /// random craters; the density is kept but not drawn from.
    /// @param craters Flagged manual and given IDs as `AppendManualCraters` does.
    /// @param surface What the craters lie on.
    /// @throws std::invalid_argument as the constructor.
    static std::shared_ptr<CraterLayer> Manual(CraterModels models, CraterList craters,
                                               CraterApplyOptions apply = {}, Surface surface = {});

    /// `dMax` (the density's largest valid diameter when unbounded) or the largest manual crater,
    /// in meters.
    double MaxFeatureDiameter() const;

    /// The craters this layer applies to a grid covering `region` (surface coordinates): generated
    /// and manual ones centered in it or within the profile's reach of it.
    /// @param spacing Sample spacing in meters; bands too small to resolve at it are skipped
    /// (`CraterGenerationOptions::minResolvedDiameter`), 0 keeps every band.
    /// @param minDiameter,maxDiameter Diameters (m) of the craters wanted: generated bands wholly
    /// outside them may be skipped (`maxBandDiameter`), so craters outside them can still come
    /// back.
    CraterList Craters(const Bounds2d& region, double spacing = 0.0, double minDiameter = 0.0,
                       double maxDiameter = std::numeric_limits<double>::infinity()) const;

    CraterFeatures CratersOver(const RasterGrid& grid, double spacing) const override;

    /// The models the layer was made with.
    const CraterModels& Models() const {
        return models_;
    }

    /// The options, with surfaces and `marginRadii` filled in.
    const CraterLayerOptions& Options() const {
        return options_;
    }

  protected:
    /// Generates and applies the craters reaching the samples. Datums are fitted to
    /// `context.ground` where given, otherwise to a grid's heights as passed in.
    /// @throws std::invalid_argument for points when the layer has a datum and no
    /// `context.ground`, or has `maxCraters` (which ranks craters over a whole grid).
    /// @throws std::domain_error as `DegradationModel::State`.
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    /// Draws craters at the density map's largest scale and thins them by the map, taking the
    /// secondaries out of a `total` map.
    void FollowDensityMap();
    /// The smallest primary (m) throwing secondaries the map measures; infinite for none.
    static double SmallestThrower(const DensityMap& map, const SecondaryCraterOptions& secondaries);
    /// The craters large enough to throw secondaries that can reach `region`.
    CraterList Throwers(const Bounds2d& region) const;
    /// The manual craters centered in `region` or within `marginRadii` of their radii of it.
    CraterList ManualCratersNear(const Bounds2d& region, double marginRadii) const;
    /// The keep share for craters over `region`: the map's, less the secondaries thrown there
    /// by the craters around it that are large enough, when they are taken out of the map.
    CraterKeepShare KeepShare(const Bounds2d& region) const;

    CraterModels models_;
    CraterLayerOptions options_;
    std::shared_ptr<const SurfaceCells> cells_;
    std::shared_ptr<CraterCellCache> cache_;
    /// The density craters are drawn at: `models_.density`, times the map's largest scale.
    std::shared_ptr<const SizeFrequencyDistribution> candidates_;
    std::shared_ptr<const DensityScale> scale_;
    /// Craters at least this large (m) throw secondaries the map measures and follow it alone.
    double throwFrom_ = std::numeric_limits<double>::infinity();
};

}  // namespace terraingen
