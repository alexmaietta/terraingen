// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `CraterPlacer`, a layer that lists the craters it applies.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/craters/shaping/attributes.h"
#include "terraingen/layers/layer.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/span.h"

#include <memory>
#include <vector>

namespace terraingen {

/// Craters and their attributes, in step.
struct CraterFeatures {
    CraterList craters;                        ///< The craters, in the order applied.
    std::vector<CraterAttributes> attributes;  ///< Each crater's attributes, in step.

    /// Appends `other`'s craters and attributes.
    void Append(const CraterFeatures& other);
};

/// `craters` with their attributes under `models` (`CraterShapeModels::Attributes`).
/// @throws std::domain_error as `DegradationModel::State`.
CraterFeatures DescribeCraters(CraterList craters, const CraterShapeModels& models);

/// A layer that places craters. Craters are a pure function of position, so the layer lists them
/// on request rather than recording them while it is applied.
class CraterPlacer {
  public:
    virtual ~CraterPlacer() = default;

    /// The craters the layer applies over `grid`, as its `Apply` there sees them.
    /// @param spacing The sample spacing in meters the craters are listed at, as a heightmap of
    /// that spacing applies them, finer than the grid's or not (`ListedSpacing`); 0 for the grid's.
    virtual CraterFeatures CratersOver(const RasterGrid& grid, double spacing) const = 0;
};

}  // namespace terraingen
