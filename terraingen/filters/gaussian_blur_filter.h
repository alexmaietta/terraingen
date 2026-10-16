/// @file
/// @brief `GaussianBlurFilter`, the whole terrain smoothed by a Gaussian of a width in metres.
#pragma once

#include "terraingen/filters/filter.h"
#include "terraingen/util/surface.h"

namespace terraingen {

/// Smooths every cell with `GaussianBlur`, its standard deviation in metres so the result
/// looks the same at every resolution. No-data cells are left unchanged.
class GaussianBlurFilter final : public Filter {
  public:
    /// A blur of standard deviation `sigma` metres; 0 leaves the heights unchanged.
    /// @param surface What the terrain lies on; a plane, as cells of a longitude-latitude grid
    /// narrow toward the poles.
    /// @throws std::invalid_argument for a negative or non-finite `sigma`, or a sphere.
    explicit GaussianBlurFilter(double sigma, Surface surface = {});

    void Apply(Raster<float>& heights, const FilterContext& context) const override;

    /// Three standard deviations, where the kernel ends, and two samples.
    double Margin(double maxFeatureDiameter, double spacing) const override;

    /// The standard deviation in metres.
    double Sigma() const {
        return sigma_;
    }

  private:
    double sigma_;
};

}  // namespace terraingen
