#pragma once

#include "terraingen/filters/filter.h"
#include "terraingen/layers/craters/apply.h"
#include "terraingen/layers/craters/population.h"
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
    /// How rims of craters too small to resolve at the sample spacing fade out, as in
    /// `ApplyCraters`.
    ResolutionFade fade{};

    bool operator==(const RimSmoothing&) const = default;
};

/// @throws std::invalid_argument for a negative or non-finite `sigmaRadii`, an invalid `fade`, a
/// `strength` outside [0, 1], or a band that does not satisfy 0 <= `innerRadii` < 1 < `outerRadii`.
void ValidateRimSmoothing(const RimSmoothing& smoothing);

/// Rounds crater rims after `ApplyCraters`: around each crater, blends the terrain towards a
/// Gaussian-smoothed copy (`GaussianBlur` with sigma = `sigmaRadii` times the radius) with weight
/// `strength` at the rim falling to 0 at `innerRadii` and `outerRadii` along `shape`.
///
/// Fresh profiles have sharp rim crests; smoothing them is a localized linear diffusion
/// (sigma^2 = 2 kappa t; Culling 1960), the process that rounds rims as craters degrade (Fassett
/// and Thomson 2014). Every crater's smoothed copy is taken from the terrain as it was before any
/// smoothing, and the blends are applied in list order, so each cell depends only on the terrain
/// within a fixed distance and tiles with a margin (`RimSmoothingFilter::Margin`) equal the whole.
/// Cells holding the no-data value are left unchanged. The work is split over up to `threads`
/// threads (0 for every hardware thread) without changing the result. Each crater's smoothing is
/// weighted by `ResolutionWeight(diameter, spacing, fade)`; `spacing` is in world units, 0 means
/// the raster's cell size, and a finer spacing is raised to it.
///
/// References:
/// - W. E. H. Culling, "Analytical theory of erosion", Journal of Geology 68(3):336-344, 1960.
///   https://doi.org/10.1086/626663
/// - C. I. Fassett and B. J. Thomson, "Crater degradation on the lunar maria: Topographic diffusion
///   and the rate of erosion on the Moon", JGR Planets 119:2255-2271, 2014.
///   https://doi.org/10.1002/2014JE004698
///
/// @throws std::invalid_argument as `ValidateRimSmoothing`, or for a negative or non-finite
/// `spacing`.
/// @throws std::domain_error if the raster's transform is not invertible.
void SmoothCraterRims(Raster<float>& raster, const CraterList& craters,
                      const RimSmoothing& smoothing = {}, double spacing = 0.0,
                      unsigned threads = 1);

/// `SmoothCraterRims` over the craters the layers placed, at the evaluation's sample spacing.
class RimSmoothingFilter final : public Filter {
  public:
    /// @throws std::invalid_argument as `ValidateRimSmoothing`.
    explicit RimSmoothingFilter(const RimSmoothing& smoothing);

    void Apply(Raster<float>& heights, double spacing,
               const TerrainFeatures& features) const override;

    void ApplyParallel(Raster<float>& heights, double spacing, const TerrainFeatures& features,
                       unsigned threads) const override;

    /// The band's outer edge plus three sigmas for the largest crater, and two samples.
    double Margin(double maxFeatureDiameter, double spacing) const override;

    const RimSmoothing& Smoothing() const {
        return smoothing_;
    }

  private:
    RimSmoothing smoothing_;
};

}  // namespace terraingen
