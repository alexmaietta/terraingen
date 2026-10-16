/// @file
/// @brief `DetailLayer`, the detail one source has above another, added to the heights.
#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/sources/source.h"

#include <memory>

namespace terraingen {

/// Adds `weight` times the detail of `full` above `smooth` at each sample: `full.HeightAt` minus
/// `smooth.HeightAt`, such as a DEM's detail above its own low-pass (`GeoTiffSourceOptions`).
/// Samples where either source has no data are left alone.
class DetailLayer final : public FormulaLayer {
  public:
    /// @param weight Share of the detail added; 1 restores it whole.
    /// @throws std::invalid_argument for a null source or a non-finite `weight`.
    DetailLayer(std::shared_ptr<const Source> full, std::shared_ptr<const Source> smooth,
                double weight = 1.0, Surface surface = {});

  protected:
    float HeightAt(float height, double x, double y, double spacing,
                   const Ground* ground) const override;

  private:
    std::shared_ptr<const Source> full_;
    std::shared_ptr<const Source> smooth_;
    double weight_;
};

}  // namespace terraingen
