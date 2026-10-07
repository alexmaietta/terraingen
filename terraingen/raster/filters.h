#pragma once

#include "terraingen/raster/raster.h"

namespace terraingen {

/// `raster` smoothed by a Gaussian kernel with standard deviation `sigmaPixels` cells, truncated at
/// three standard deviations and applied separably along rows and columns.
///
/// Cells holding the raster's no-data value keep it and do not contribute to their neighbours: the
/// kernel is renormalized over valid cells (normalized convolution), which also keeps the edges of
/// the raster from being pulled towards zero.
///
/// Gaussian smoothing of a surface with standard deviation sigma is the solution of linear
/// diffusion of that surface for time t with diffusivity kappa where sigma^2 = 2 kappa t, the model
/// of hillslope and crater degradation by Culling (1960) and Fassett and Thomson (2014).
///
/// References:
/// - H. Knutsson and C.-F. Westin, "Normalized and differential convolution", Proc. IEEE Conference
///   on Computer Vision and Pattern Recognition, 515-523, 1993.
///   https://doi.org/10.1109/CVPR.1993.341081
/// - W. E. H. Culling, "Analytical theory of erosion", Journal of Geology 68(3):336-344, 1960.
///   https://doi.org/10.1086/626663
///
/// @throws std::invalid_argument unless `sigmaPixels` is finite and not negative; 0 returns a copy.
Raster<float> GaussianBlur(const Raster<float>& raster, double sigmaPixels);

/// `GaussianBlur` with standard deviations `sigmaColumns` along rows and `sigmaRows` along
/// columns, in pixels, as on a longitude-latitude grid where cells are narrower east to west.
/// @throws std::invalid_argument unless both are finite and not negative; both 0 returns a copy.
Raster<float> GaussianBlur(const Raster<float>& raster, double sigmaColumns, double sigmaRows);

}  // namespace terraingen
