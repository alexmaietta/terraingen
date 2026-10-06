#pragma once

#include "terraingen/features/craters/apply.h"
#include "terraingen/features/craters/population.h"
#include "terraingen/raster/raster.h"

namespace terraingen {

/// Settings for `SmoothCraterRims`.
struct RimSmoothing {
    /// Gaussian standard deviation as a fraction of each crater's radius; 0 disables smoothing.
    double sigmaRadii = 0.05;
    /// Band around the rim that is smoothed, in crater radii; must contain the rim at 1.
    double innerRadii = 0.8;
    double outerRadii = 1.2;
    /// How the smoothing fades from full at the rim to none at the band's edges.
    BlendShape shape = BlendShape::Smootherstep;
    /// Fraction of the smoothed height used at the rim, in [0, 1].
    double strength = 1.0;
    /// Craters narrower than this many pixels are skipped, as in `ApplyCraters`.
    double minDiameterPixels = 3.0;

    bool operator==(const RimSmoothing&) const = default;
};

/// @throws std::invalid_argument for a negative or non-finite `sigmaRadii` or
/// `minDiameterPixels`, a `strength` outside [0, 1], or a band that does not satisfy
/// 0 <= `innerRadii` < 1 < `outerRadii`.
void ValidateRimSmoothing(const RimSmoothing& smoothing);

/// Rounds crater rims after `ApplyCraters`: around each crater, blends the terrain towards a
/// Gaussian-smoothed copy (`GaussianBlur` with sigma = `sigmaRadii` times the radius) with weight
/// `strength` at the rim falling to 0 at `innerRadii` and `outerRadii` along `shape`.
///
/// Fresh profiles have sharp rim crests; smoothing them is a localized linear diffusion
/// (sigma^2 = 2 kappa t; Culling 1960), the process that rounds rims as craters degrade (Fassett
/// and Thomson 2014). Craters are smoothed in list order, each against the terrain as left by the
/// previous ones; cells holding the no-data value are left unchanged.
///
/// References:
/// - W. E. H. Culling, "Analytical theory of erosion", Journal of Geology 68(3):336-344, 1960.
///   https://doi.org/10.1086/626663
/// - C. I. Fassett and B. J. Thomson, "Crater degradation on the lunar maria: Topographic diffusion
///   and the rate of erosion on the Moon", JGR Planets 119:2255-2271, 2014.
///   https://doi.org/10.1002/2014JE004698
///
/// @throws std::invalid_argument for a negative or non-finite `sigmaRadii` or
/// `minDiameterPixels`, a `strength` outside [0, 1], or a band that does not satisfy
/// 0 <= `innerRadii` < 1 < `outerRadii`.
/// @throws std::domain_error if the raster's transform is not invertible.
void SmoothCraterRims(Raster<float>& raster, const CraterList& craters,
                      const RimSmoothing& smoothing = {});

}  // namespace terraingen
