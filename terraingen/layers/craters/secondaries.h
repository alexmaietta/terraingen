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
    /// @param primaries The layer whose craters throw secondaries.
    /// @param apply How secondaries are applied; without a datum.
    /// @throws std::invalid_argument for a null layer or model, invalid options, or apply options
    /// with a datum.
    SecondaryCraterLayer(std::shared_ptr<const CraterLayer> primaries, CraterShapeModels models,
                         SecondaryCraterOptions options, CraterApplyOptions apply = {});

    /// The largest secondary the layer places, in metres.
    double MaxFeatureDiameter() const;

    /// The secondaries a grid covering `region` (surface coordinates) gets: those centred in it or
    /// within the profile's reach of it.
    /// @param spacing Sample spacing in metres; 0 for every size.
    /// @throws std::invalid_argument if more than `maxExpected` are expected.
    CraterList Secondaries(const Bounds2d& region, double spacing = 0.0) const;

    /// `Secondaries`, also filling `outlines` with the ellipses of the elongated ones by ID.
    CraterList Secondaries(const Bounds2d& region, double spacing,
                           std::unordered_map<std::uint64_t, SecondaryOutline>& outlines) const;

    /// The outer range from its centre, in metres, of the secondaries of a primary of `diameter`
    /// metres.
    double OuterRange(double diameter) const;

    /// A secondary's attributes in the state the degradation model gives it.
    /// @throws std::domain_error as `DegradationModel::State`.
    CraterAttributes Attributes(const Crater& crater) const;

    CraterFeatures CratersOver(const RasterGrid& grid, double spacing) const override;

    /// The options the layer was made with.
    const SecondaryCraterOptions& Options() const {
        return options_;
    }

  protected:
    /// Applies the secondaries reaching the samples.
    /// @throws std::invalid_argument if more than `maxExpected` secondaries are expected.
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    std::shared_ptr<const CraterLayer> primaries_;
    CraterShapeModels models_;
    SecondaryCraterOptions options_;
    CraterApplyOptions apply_;
};

}  // namespace terraingen
