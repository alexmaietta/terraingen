// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Surface material classes, and `MaterialSource`, the layers that lay one down.
#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace terraingen {

/// What the surface is made of at a point.
enum class MaterialClass : std::uint8_t {
    Soil,     ///< Unconsolidated regolith, the default everywhere.
    Sand,     ///< Wind-sorted sand: dunes, ripples, sand sheets and fill.
    Dust,     ///< Bright airfall dust.
    Bedrock,  ///< Exposed bedrock and outcrop.
    Rock,     ///< Loose rocks and boulders.
};

/// The number of `MaterialClass` values.
inline constexpr std::size_t kMaterialClasses = 5;

/// The share of each material class at a point, indexed by `MaterialClass`; they sum to 1.
using MaterialShares = std::array<double, kMaterialClasses>;

/// Shares all of soil, the default before anything covers it.
MaterialShares SoilShares();

/// Lays `cover`, in [0, 1], of class `material` over `shares`: every other share shrinks by
/// (1 - cover), so the shares still sum to 1.
void CoverWith(MaterialShares& shares, MaterialClass material, double cover);

/// A layer that lays a material over the surface beneath it, such as sand from dunes.
///
/// `MaterialCoverAt` must be a function of position like the layer's heights, so material maps
/// agree on every grid and tile.
class MaterialSource {
  public:
    virtual ~MaterialSource() = default;

    /// The class the layer lays down.
    virtual MaterialClass Material() const = 0;

    /// How fully the layer covers world (x, y) with its material, in [0, 1], evaluated as its
    /// heights are in `context`: its sample spacing in meters, surface, and the ground beneath
    /// it, which layers that read it need.
    virtual double MaterialCoverAt(double x, double y, const LayerContext& context) const = 0;

    /// `MaterialCoverAt` at every cell center of `grid` on `context.threads` threads, at
    /// `context.spacing`, at least the grid's cell size. By default cell by cell; layers that
    /// place features override it to find them once for the whole grid.
    virtual Raster<double> MaterialCoverOver(const RasterGrid& grid,
                                             const LayerContext& context) const;
};

}  // namespace terraingen
