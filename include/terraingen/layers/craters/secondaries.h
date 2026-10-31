// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `SecondaryCraterLayer`, secondary craters around the craters of a `CraterLayer`.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/crater_placer.h"
#include "terraingen/layers/craters/rays.h"
#include "terraingen/layers/craters/secondary_model.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/layer.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>

namespace terraingen {

/// The ellipse an elongated secondary is stretched to.
struct SecondaryOutline {
    double ellipticity = 1.0;  ///< Long over short axis.
    double heading = 0.0;  ///< Long axis, radians counter-clockwise from east, toward the primary.
};

/// The farthest `shapes` stretch a secondary's rim, as a factor on its radius.
double SecondaryStretch(const SecondaryShapes& shapes);

/// Secondary craters around the craters of a `CraterLayer`, applied additively with
/// `ApplyCraters`.
///
/// Drawn per primary in polar cells keyed to its ID, size band, ring and sector, so a tile holds
/// the same secondaries as any larger grid; bands too small for the spacing are skipped.
class SecondaryCraterLayer final : public Layer, public CraterPlacer {
  public:
    /// The secondaries of the craters of `primaries`.
    /// @param apply How secondaries are applied; without a datum.
    /// @param base The surface, which must be the primaries', and the source.
    /// @throws std::invalid_argument for a null layer or degradation, a mismatched surface, a
    /// rejected profile, invalid options, a datum, or options a density-mapped primary lacks.
    SecondaryCraterLayer(std::shared_ptr<const CraterLayer> primaries, CraterShapeModels models,
                         SecondaryCraterOptions options, CraterApplyOptions apply = {},
                         TerrainBase base = {});

    /// Secondaries shaped as the primaries are (`CraterModels::Shape`).
    /// @throws std::invalid_argument as the other constructor.
    SecondaryCraterLayer(std::shared_ptr<const CraterLayer> primaries,
                         SecondaryCraterOptions options, CraterApplyOptions apply = {},
                         TerrainBase base = {});

    /// The largest secondary the layer places, in meters.
    double MaxFeatureDiameter() const;

    /// The secondaries a grid covering `region` (surface coordinates) gets: those centered in it or
    /// within the profile's reach of it.
    /// @param spacing Sample spacing in meters; 0 for every size.
    /// @throws std::invalid_argument if more than `maxExpected` are expected.
    CraterList Secondaries(const Bounds2d& region, double spacing = 0.0) const;

    /// `Secondaries`, also filling `outlines` with the ellipses of the elongated ones by ID.
    CraterList Secondaries(const Bounds2d& region, double spacing,
                           std::unordered_map<std::uint64_t, SecondaryOutline>& outlines) const;

    /// The outer range from its center, in meters, of the secondaries of a primary of `diameter`
    /// meters.
    double OuterRange(double diameter) const;

    CraterFeatures CratersOver(const RasterGrid& grid, double spacing) const override;

    /// The options the layer was made with.
    const SecondaryCraterOptions& Options() const {
        return options_;
    }

    std::optional<TerrainBase> LayerBase() const override {
        return TerrainBase{apply_.surface, source_};
    }

    /// The primaries, which must come before this layer.
    std::vector<const Layer*> Dependencies() const override {
        return {primaries_.get()};
    }

    /// The layer with its seed `ReseedWord(seed, 0)` and the apply seeds of the irregularity,
    /// noise, texture, slope, terraces and peaks `ReseedWord(seed, 1)` to `ReseedWord(seed, 6)`.
    LayerPtr Reseeded(std::uint64_t seed) const override;

    /// A copy reading the replacement of its primaries; null when they are not replaced.
    LayerPtr Rebound(const LayerReplacements& replaced) const override;

    /// The layer built again from its models and options on `base`.
    /// @throws std::invalid_argument as the constructor.
    LayerPtr Rebased(const TerrainBase& base) const override;

  protected:
    /// Applies the secondaries reaching the samples.
    /// @throws std::invalid_argument if more than `maxExpected` secondaries are expected.
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    std::shared_ptr<const CraterLayer> primaries_;
    CraterShapeModels models_;
    SecondaryCraterOptions options_;
    CraterApplyOptions apply_;
    std::shared_ptr<const Source> source_;
    std::shared_ptr<const SurfaceField> limit_;
};

}  // namespace terraingen
