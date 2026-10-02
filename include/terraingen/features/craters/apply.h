#pragma once

#include "terraingen/features/craters/degradation/degradation_model.h"
#include "terraingen/features/craters/population.h"
#include "terraingen/features/craters/profiles/profile.h"
#include "terraingen/raster/raster.h"

namespace terraingen {

/// The order in which overlapping craters are applied; later craters overprint earlier ones.
enum class CraterOrder {
    OldestFirst,   ///< Decreasing age, then decreasing diameter: the physical order.
    LargestFirst,  ///< Decreasing diameter, then decreasing age.
};

/// Settings for `ApplyCraters`.
struct CraterApplyOptions {
    CraterOrder order = CraterOrder::OldestFirst;
    /// Craters narrower than this many pixels are skipped, since they cannot be resolved.
    double minDiameterPixels = 3.0;
};

/// Applies `craters` to `raster`, whose `GeoTransform` maps pixels to the craters' surface
/// coordinates, shaping each crater with `profile` in the state `degradation` gives it.
///
/// Each crater is applied in turn. A plane is fitted by least squares to the heights inside its
/// rim, so craters on slopes keep the slope. Within the rim the terrain is replaced by that plane
/// plus the profile, erasing older relief as an impact excavates it; outside the rim the profile's
/// rim and ejecta are added to the existing terrain, with a smooth blend between the two over 1
/// to 1.2 radii. Cells holding the raster's no-data value are left unchanged and excluded from the
/// fit.
///
/// Craters overlapping the raster edge are fitted to the cells available, so a crater near the edge
/// of one raster can differ slightly from the same crater in a larger raster.
///
/// @throws std::invalid_argument if `minDiameterPixels` is negative or not finite.
/// @throws std::domain_error if the raster's transform is not invertible.
void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterProfile& profile,
                  const DegradationModel& degradation, const CraterApplyOptions& options = {});

/// `ApplyCraters` with every crater fresh (`NoDegradation`).
void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterProfile& profile,
                  const CraterApplyOptions& options = {});

}  // namespace terraingen
