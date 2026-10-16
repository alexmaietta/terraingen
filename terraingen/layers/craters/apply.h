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
#include "terraingen/layers/sample_block.h"
#include "terraingen/raster/raster.h"

#include <memory>

namespace terraingen {

/// @brief The models craters are shaped with: their profile and the degradation state it is drawn
/// in.
struct CraterShapeModels {
    std::shared_ptr<const CraterProfile> profile;         ///< Height by distance from the centre.
    std::shared_ptr<const DegradationModel> degradation;  ///< Each crater's state.
};

/// @brief How `ApplyCraters` samples a raster.
struct CraterSampling {
    /// Sample spacing in world units; 0 or anything finer means the raster's cell size.
    double spacing = 0.0;
    unsigned threads = 1;  ///< Worker threads, 0 for all; the result does not depend on it.
    /// The ground datums are fitted to; empty for the raster as passed in, interpolated
    /// bilinearly.
    HeightSampler ground{};
};

/// @brief The height at a point after one crater, bit-identical to what `ApplyCraters` does to a
/// cell centred there.
///
/// Adds the profile weighted by the crater's resolution; with a datum, also moves the terrain
/// toward the datum by `interiorReplacement` times the blend weight.
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
/// Each crater is weighted by `ResolutionWeight`; with a datum, each is fitted to `sampling.ground`
/// plus the relief of the craters before it. No-data cells are left unchanged and excluded from
/// the fit.
/// @param raster The heights to change; its transform maps pixels to the craters' surface
/// coordinates.
/// @param craters The craters; with a datum, those within `DatumMargin` of the raster.
/// @param models The profile and degradation; neither may be null.
/// @param options How the craters are applied.
/// @param sampling The sample spacing, threads and ground.
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
