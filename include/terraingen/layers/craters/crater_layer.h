// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `CraterLayer`, craters generated over the grid being evaluated and applied to it.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/crater_placer.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/craters/secondary_model.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/craters/shaping/attributes.h"
#include "terraingen/layers/density_map.h"
#include "terraingen/layers/layer.h"
#include "terraingen/sources/terrain_base.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

class CraterCellCache;

/// The models a crater layer generates and shapes craters with.
struct CraterModels {
    /// Craters per m^2 by diameter; may be null for a layer that generates none.
    std::shared_ptr<const SizeFrequencyDistribution> density;
    /// Each crater's age; null takes the degradation's `AgeModel`, so ages are given once, and may
    /// stay null for a layer that generates no craters.
    std::shared_ptr<const CraterAgeModel> ages;
    std::shared_ptr<const DegradationModel> degradation;  ///< Each crater's state.
    /// Height by distance; null takes the one the degradation requires, else Martin, Parkes and
    /// Dunstan's (`MatchedProfile`).
    std::shared_ptr<const CraterProfile> profile;

    /// The profile and degradation, the models that shape the craters.
    CraterShapeModels Shape() const {
        return {profile, degradation};
    }
};

/// The crater models a configuration's crater layer has by default: a 3.5 Gyr extended Neukum
/// production surface capped by Hartmann equilibrium (Fassett 2022,
/// https://doi.org/10.1029/2022JE007510), steady-state ages on it, Martin-Parkes-Dunstan
/// profiles, and diffusion matched to the ages from the lunar fresh morphometry.
CraterModels DefaultCraterModels();

/// Craters a coarser level holds that the layer's source softens, stamped again sharp: those a
/// coarser run listed (`ReadCraterList`) over the heights it wrote, which the source now
/// upsamples, or a catalog of the craters a DEM holds. Each one the source resolves replaces
/// (`Crater::replace`) what the source holds there, with its ID, age, depth and rim as listed, the
/// rim only where the profile takes one (`TakesRimHeight`);
/// smaller ones are left to generation, which fills the gaps beneath them, and generated craters
/// the inherited ones stand in for are dropped. Without a source every inherited crater is
/// stamped.
struct InheritedCraters {
    CraterList craters;  ///< The coarser level's craters; empty for none.
    /// Source cells across from which an inherited crater is stamped again and generated craters
    /// are dropped; unset follows `CraterLayerOptions::sourceResolvesPixels`, and with both unset
    /// every inherited crater is stamped. Lower than that to clean up the smaller craters a
    /// coarser run of the same layer drew blurred into the source.
    std::optional<double> minSourcePixels{};
    /// Source cells across from which inherited craters are left as the source holds them, since
    /// interpolation barely softens them; unset stamps every one from `minSourcePixels`.
    std::optional<double> maxSourcePixels{};
};

/// Checks inherited crater settings.
/// @throws std::invalid_argument for a source pixel count that is set and not positive and finite,
/// or a `maxSourcePixels` not above `minSourcePixels`.
void ValidateInheritedCraters(const InheritedCraters& inherited);

/// How a `CraterLayer` generates and applies its craters.
struct CraterLayerOptions {
    /// Draws random craters from the density; off places only the manual craters in
    /// `generation`, which are placed either way.
    bool generateRandom = true;
    std::uint32_t seed = 1;  ///< Varies the generated craters.
    /// Diameter range, spacing, manual craters and cap; `marginRadii` is set from the profile's
    /// reach. By default 2.5 m, where the extended Neukum production function begins, to 250 m.
    CraterGenerationOptions generation = [] {
        CraterGenerationOptions options;
        options.dMin = 2.5;
        options.dMax = 250.0;
        return options;
    }();
    /// How the craters are shaped into the terrain; by default `DefaultCraterApply`.
    CraterApplyOptions apply = DefaultCraterApply();
    /// Most craters to keep generated cells of (`CraterCellCache`); 0 regenerates cells every time.
    std::size_t cacheCraters = 4000000;
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
    /// Craters of a coarser level stamped again sharp where the source softened them; none by
    /// default.
    InheritedCraters inherited{};
};

/// Craters generated over the grid being evaluated (`GenerateCraters`) and applied to it
/// (`ApplyCraters`) at the evaluation's spacing.
///
/// Generated craters are the same in a tile as in any larger grid, unless `maxCraters` caps them
/// over the whole grid; bands too small to resolve are not generated.
class CraterLayer final : public Layer, public CraterPlacer {
  public:
    /// @param models The density and ages may be null without `generateRandom` or a density map,
    /// the ages may be null for those the degradation has, and the profile as `MatchedProfile`
    /// takes it.
    /// @param base What the craters lie on, copied into `generation.surface` and `apply.surface`,
    /// and the source, which with `options.sourceResolvesPixels` holds the craters it resolves.
    /// @throws std::invalid_argument for a missing model, a profile `MatchedProfile` rejects, or
    /// invalid options.
    CraterLayer(CraterModels models = DefaultCraterModels(), CraterLayerOptions options = {},
                TerrainBase base = {});

    /// A layer of only `craters`, shaped by `models` and `apply`: `generateRandom` off, so no
    /// random craters, and the density and ages are not used.
    /// @param craters Flagged manual and given IDs as `AppendManualCraters` does.
    /// @throws std::invalid_argument as the constructor.
    static std::shared_ptr<CraterLayer> Manual(CraterList craters,
                                               CraterModels models = DefaultCraterModels(),
                                               CraterApplyOptions apply = DefaultCraterApply(),
                                               TerrainBase base = {});

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
    /// @param threads Threads the cells are drawn on; the craters do not depend on it.
    CraterList Craters(const Bounds2d& region, double spacing = 0.0, double minDiameter = 0.0,
                       double maxDiameter = std::numeric_limits<double>::infinity(),
                       unsigned threads = 1) const;

    CraterFeatures CratersOver(const RasterGrid& grid, double spacing) const override;

    /// The models the layer was made with.
    const CraterModels& Models() const {
        return models_;
    }

    /// The options, with surfaces and `marginRadii` filled in.
    const CraterLayerOptions& Options() const {
        return options_;
    }

    std::optional<Surface> LayerSurface() const override {
        return surface_;
    }

  protected:
    /// Generates and applies the craters reaching the samples. Datums are fitted to
    /// `context.ground` where given, otherwise to a grid's heights as passed in.
    /// @throws std::invalid_argument for points when the layer has a datum and no
    /// `context.ground`, or has `maxCraters` (which ranks craters over a whole grid).
    /// @throws std::domain_error as `DegradationModel::State`.
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    /// Adds the inherited craters `source` resolves to the manual ones, replacing, and drops the
    /// generated craters they stand in for.
    void Inherit(const std::shared_ptr<const Source>& source);
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
    /// Sets `generation.bandCount` to read the bands' counts from `bandCounts_`.
    void CountBands(CraterGenerationOptions& generation) const;

    /// A band's expected craters per square meter at the density of `candidates_`.
    struct BandCount {
        double low = 0.0;
        double high = 0.0;
        double count = 0.0;
    };

    CraterModels models_;
    CraterLayerOptions options_;
    Surface surface_;
    std::shared_ptr<const SurfaceCells> cells_;
    std::shared_ptr<CraterCellCache> cache_;
    /// The density craters are drawn at: `models_.density`, times the map's largest scale.
    std::shared_ptr<const SizeFrequencyDistribution> candidates_;
    std::shared_ptr<const DensityScale> scale_;
    /// The counts of the bands generation draws, so point queries do not compute them each time.
    std::vector<BandCount> bandCounts_;
    /// Craters at least this large (m) throw secondaries the map measures and follow it alone.
    double throwFrom_ = std::numeric_limits<double>::infinity();
    /// How far (m) beyond a grid the replacing manual craters can change it (`ReplacementMargin`).
    double replacementMargin_ = 0.0;
};

}  // namespace terraingen
