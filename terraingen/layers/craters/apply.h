/// @file
/// @brief Applying craters to heights: one crater at a point, or a list over a raster.
#pragma once

#include "terraingen/layers/craters/apply_options.h"
#include "terraingen/layers/craters/datum.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/craters/prepared.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/ground.h"
#include "terraingen/raster/raster.h"

#include <memory>

namespace terraingen {

/// The models craters are shaped with: their profile and the degradation state it is drawn in.
struct CraterShapeModels {
    std::shared_ptr<const CraterProfile> profile;
    std::shared_ptr<const DegradationModel> degradation;
};

/// How `ApplyCraters` samples a raster.
struct CraterSampling {
    /// Sample spacing in world units; 0 means the raster's cell size, and a finer spacing is
    /// raised to it.
    double spacing = 0.0;
    /// Threads to split the work over; 0 for every hardware thread. The result does not depend on
    /// it.
    unsigned threads = 1;
    /// The ground datums are fitted to; empty for the raster as passed in, interpolated
    /// bilinearly.
    HeightSampler ground{};
};

/// @brief The height at a point after one crater: what `ApplyCraters` does to a cell centred
/// there, shared with point queries so both give the same bits.
///
/// Without a datum the profile, weighted by the crater's resolution, is added. With one, the
/// terrain is also moved toward the datum by `interiorReplacement` times the blend weight.
/// @param height The height before the crater.
/// @param x Surface x of the point.
/// @param y Surface y of the point.
/// @param crater The crater, with its datum when the options have one.
/// @param profile The profile shaping it.
/// @param options The apply options.
/// @return The height after the crater; `height` beyond the profile's reach.
float ApplyCraterTo(float height, double x, double y, const PreparedCrater& crater,
                    const CraterProfile& profile, const CraterApplyOptions& options);

/// @brief Applies craters to a raster, in turn, in the order `options` gives.
///
/// Each crater is shaped by `models.profile` in the state `models.degradation` gives it, weighted
/// by `ResolutionWeight` at the sample spacing. With a datum, each datum is fitted
/// (`FitCraterDatum`) to `sampling.ground` plus the relief of the craters before it
/// (`DatumGround`); craters off the raster still count toward the datums of craters on it, so
/// callers pass the craters within `DatumMargin` of the raster. Every cell depends only on the
/// craters reaching it and their datums, so with a ground that covers the footprints a tile of a
/// larger raster gets the same cells as the larger raster. Cells holding the raster's no-data
/// value are left unchanged and excluded from the fit. See SCIENCE.md, "Crater datums and
/// blends".
/// @param raster The heights to change; its transform maps pixels to the craters' surface
/// coordinates.
/// @param craters The craters.
/// @param models The profile and degradation; neither may be null.
/// @param options How the craters are applied.
/// @param sampling The sample spacing, threads and ground.
/// @throws std::invalid_argument for invalid `options` or a negative or non-finite spacing.
/// @throws std::domain_error if the raster's transform is not invertible.
void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterShapeModels& models,
                  const CraterApplyOptions& options = {}, const CraterSampling& sampling = {});

}  // namespace terraingen
