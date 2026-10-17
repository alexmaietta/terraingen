/// @file
/// @brief Applying craters to heights: one crater at a point, or a list over a raster.
#pragma once

#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/craters/shaping/apply_options.h"
#include "terraingen/layers/craters/shaping/datum.h"
#include "terraingen/layers/craters/shaping/prepared.h"
#include "terraingen/layers/evaluation_cache.h"
#include "terraingen/layers/ground.h"
#include "terraingen/layers/sample_block.h"
#include "terraingen/raster/raster.h"

#include <functional>
#include <memory>

namespace terraingen {

/// The models craters are shaped with: their profile and the degradation state it is drawn in.
struct CraterShapeModels {
    std::shared_ptr<const CraterProfile> profile;         ///< Height by distance from the centre.
    std::shared_ptr<const DegradationModel> degradation;  ///< Each crater's state.
};

/// How `ApplyCraters` samples a raster.
struct CraterSampling {
    /// Sample spacing in world units; 0 or anything finer means the raster's cell size.
    double spacing = 0.0;
    unsigned threads = 1;  ///< Worker threads, 0 for all; the result does not depend on it.
    /// The ground datums are fitted to; empty for the raster as passed in, interpolated
    /// bilinearly.
    HeightSampler ground{};
    /// Changes each crater's outline after its irregularity, such as secondaries stretched toward
    /// their primary; empty leaves outlines as they are. Must be deterministic and thread-safe.
    std::function<void(const Crater&, CraterOutline&)> reshape{};
    /// Where point queries keep the datums they fit, shared across batches; null to fit each time.
    EvaluationCache* cache = nullptr;
    const void* owner = nullptr;  ///< The layer the cached datums belong to.
};

/// How far, in crater radii, a crater changes the terrain: the profile's reach, or the ejecta's
/// when `options.texture` draws beyond the rim and they reach further.
double CraterReachRadii(const CraterProfile& profile, const CraterApplyOptions& options);

/// The height at surface (x, y) after one crater, bit-identical to what `ApplyCraters` does to a
/// cell centred there; `height` beyond its reach (`CraterReachRadii`).
///
/// Adds the profile, noise and texture weighted by the crater's resolution; with a datum, also
/// moves the terrain toward the datum by `interiorReplacement` times the blend weight, or by the
/// texture's mantling outside the rim where that is more.
/// @param crater With its datum when the options have one.
float ApplyCraterTo(float height, double x, double y, const PreparedCrater& crater,
                    const CraterProfile& profile, const CraterApplyOptions& options);

/// Applies craters to a raster, in turn, in the order `options` gives.
///
/// Each crater is weighted by `ResolutionWeight`; with a datum, each is fitted to `sampling.ground`
/// plus the relief of the craters before it. No-data cells are left unchanged and excluded from
/// the fit.
/// @param raster Its transform maps pixels to the craters' surface coordinates.
/// @param craters With a datum, those within `DatumMargin` of the raster.
/// @param models Neither may be null.
/// @throws std::invalid_argument for invalid `options` or a negative or non-finite spacing.
/// @throws std::domain_error if the raster's transform is not invertible.
void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterShapeModels& models,
                  const CraterApplyOptions& options = {}, const CraterSampling& sampling = {});

/// `ApplyCraters` on a sample block: a grid as above, or scattered points, each of which ends at
/// the same height as a grid cell centred there.
/// @throws std::invalid_argument as above; for points also without a positive spacing, or with a
/// datum and no `sampling.ground`.
void ApplyCraters(SampleBlock& samples, const CraterList& craters, const CraterShapeModels& models,
                  const CraterApplyOptions& options = {}, const CraterSampling& sampling = {});

}  // namespace terraingen
