#pragma once

#include "terraingen/core/raster.h"
#include "terraingen/kernels/fbm.h"

namespace terraingen {

/// Checks that `params` describe a usable fBm surface.
/// @throws std::invalid_argument naming the first invalid field.
void ValidateFbmParams(const FbmParams& params);

/// Sets every cell of `raster` to the fBm height at the cell's centre, in world units.
///
/// Cell centres come from the raster's `GeoTransform`, so rasters that tile a larger grid on the
/// same lattice produce the same values as the larger grid.
/// @throws std::invalid_argument if `params` are invalid.
void FillFbm(Raster<float>& raster, const FbmParams& params);

}  // namespace terraingen
