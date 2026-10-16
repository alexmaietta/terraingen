/// @file
/// @brief Rounding crater rims by blending them toward a blurred copy of the terrain.
#pragma once

#include "terraingen/filters/filter.h"
#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/fade.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"

namespace terraingen {

/// @brief How `SmoothCraterRims` rounds crater rims.
struct RimSmoothing {
    /// Gaussian standard deviation as a fraction of each crater's radius; 0 disables smoothing.
    double sigmaRadii = 0.05;
    double innerRadii = 0.8;  ///< Inner edge of the smoothed band, in crater radii; in [0, 1).
    double outerRadii = 1.2;  ///< Outer edge of the smoothed band, in crater radii; > 1.
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
/// Every smoothed copy is taken from the unsmoothed terrain and the blends applied in list order,
/// so tiles with a margin (`RimSmoothingFilter::Margin`) equal the whole. Each crater's smoothing
/// is weighted by its `ResolutionWeight`; no-data cells are left unchanged. On a sphere the raster
/// is in longitude and latitude, distances are great-circle, and blurs are round in metres.
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

/// `SmoothCraterRims` over the craters the layers before it apply (`PlacedCraters` of
/// `FilterContext::layers`), at the evaluation's sample spacing.
class RimSmoothingFilter final : public Filter {
  public:
    /// @param smoothing How rims are smoothed.
    /// @param surface What the craters lie on.
    /// @throws std::invalid_argument as `ValidateRimSmoothing`.
    explicit RimSmoothingFilter(const RimSmoothing& smoothing, const Surface& surface = {});

    /// @throws std::invalid_argument as `SmoothCraterRims`.
    /// @throws std::domain_error if the raster's transform is not invertible.
    void Apply(Raster<float>& heights, const FilterContext& context) const override;

    /// The band's outer edge plus three sigmas for the largest crater, and two samples.
    double Margin(double maxFeatureDiameter, double spacing) const override;

    /// How rims are smoothed.
    const RimSmoothing& Smoothing() const {
        return smoothing_;
    }

  private:
    RimSmoothing smoothing_;
    Surface surface_;
};

}  // namespace terraingen
