/// @file
/// @brief Rounding crater rims by blending them toward a blurred copy of the terrain.
#pragma once

#include "terraingen/filters/filter.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/fade.h"
#include "terraingen/raster/raster.h"

namespace terraingen {

/// How `SmoothCraterRims` rounds crater rims.
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

/// @brief Checks rim smoothing settings.
/// @param smoothing The settings to check.
/// @throws std::invalid_argument for a negative or non-finite `sigmaRadii`, an invalid `fade`, a
/// `strength` outside [0, 1], or a band that does not satisfy 0 <= `innerRadii` < 1 < `outerRadii`.
void ValidateRimSmoothing(const RimSmoothing& smoothing);

/// @brief Rounds crater rims after `ApplyCraters`: around each crater, blends the terrain toward
/// a Gaussian-smoothed copy (`GaussianBlur`, sigma `sigmaRadii` times the radius) with weight
/// `strength` at the rim falling to 0 at `innerRadii` and `outerRadii` along `shape`.
///
/// Every crater's smoothed copy is taken from the terrain as it was before any smoothing, and the
/// blends are applied in list order, so each cell depends only on the terrain within a fixed
/// distance and tiles with a margin (`RimSmoothingFilter::Margin`) equal the whole. Each crater's
/// smoothing is weighted by its `ResolutionWeight`. Cells holding the no-data value are left
/// unchanged. On a sphere the raster is in longitude and latitude, distances are great-circle, and
/// each blur is narrower in columns by the cosine of its latitude, so it is round in metres. See
/// SCIENCE.md, "Rim smoothing".
/// @param raster The heights to smooth in place.
/// @param craters The craters whose rims are smoothed.
/// @param smoothing How they are smoothed.
/// @param spacing Sample spacing in metres; 0 means the raster's cell size, and a finer spacing is
/// raised to it.
/// @param threads Threads to split the work over; 0 for every hardware thread. The result does not
/// depend on it.
/// @param surface What the craters lie on.
/// @throws std::invalid_argument as `ValidateRimSmoothing`, or for a negative or non-finite
/// `spacing`.
/// @throws std::domain_error if the raster's transform is not invertible.
void SmoothCraterRims(Raster<float>& raster, const CraterList& craters,
                      const RimSmoothing& smoothing = {}, double spacing = 0.0,
                      unsigned threads = 1, const Surface& surface = {});

/// `SmoothCraterRims` over the craters the layers placed, at the evaluation's sample spacing.
class RimSmoothingFilter final : public Filter {
  public:
    /// @brief Builds the filter.
    /// @param smoothing How rims are smoothed.
    /// @param surface What the craters lie on.
    /// @throws std::invalid_argument as `ValidateRimSmoothing`.
    explicit RimSmoothingFilter(const RimSmoothing& smoothing, const Surface& surface = {});

    void Apply(Raster<float>& heights, double spacing,
               const TerrainFeatures& features) const override;

    void ApplyParallel(Raster<float>& heights, double spacing, const TerrainFeatures& features,
                       unsigned threads) const override;

    /// @brief How far the filter reads around a cell.
    /// @param maxFeatureDiameter The largest crater, in metres.
    /// @param spacing The sample spacing, in metres.
    /// @return The band's outer edge plus three sigmas for the largest crater, and two samples.
    double Margin(double maxFeatureDiameter, double spacing) const override;

    const RimSmoothing& Smoothing() const {
        return smoothing_;
    }

  private:
    RimSmoothing smoothing_;
    Surface surface_;
};

}  // namespace terraingen
