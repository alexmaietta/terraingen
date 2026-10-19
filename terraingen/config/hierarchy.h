/// @file
/// @brief The grids, outputs and manifest of a multi-resolution hierarchy (`HierarchySpec`).
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/surface.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace terraingen::config {

/// One grid of a hierarchy.
struct HierarchyGrid {
    std::size_t region = 0;  ///< From 0; the base is region 0.
    std::size_t level = 0;   ///< 0 for the base.
    GridRegionSpec grid{};
    std::optional<std::size_t> parent{};  ///< Index of the grid it lies in; none for the base.
    std::vector<std::size_t> children{};  ///< Indices of the grids that lie in it.
    std::string name;                     ///< The hierarchy's path with its numbers in.
};

/// The grids of `hierarchy`: the base first, then each region's levels from coarse to fine.
///
/// Each region's centre snaps to the base cell centre nearest it. Level l has cells of the base's
/// pixel size over factor^l, an odd number across that ends on cell centres of level l - 1, at
/// least one parent cell inside the parent's edge cells.
/// @param base The base's grid, level 0 when the hierarchy gives none.
/// @throws ConfigError under "hierarchy" for a path without "{region}" and "{level}", no base
/// grid, a base grid without square cells, a level beyond the one it lies in or over the grid
/// size limit, or regions whose squares overlap.
std::vector<HierarchyGrid> HierarchyGrids(const HierarchySpec& hierarchy,
                                          const std::optional<RasterGrid>& base,
                                          const Surface& surface);

/// The file `product` of `grid` is written to: its name with the product's suffix and extension.
std::string HierarchyFile(const HierarchySpec& hierarchy, const HierarchyGrid& grid,
                          HierarchyProduct product);

/// The outputs that write every product of `hierarchy` over every grid, in the order of `grids`;
/// terrain meshes are cut out under each grid's children and framed at the base's centre.
std::vector<OutputSpec> HierarchyOutputs(const HierarchySpec& hierarchy,
                                         const std::vector<HierarchyGrid>& grids,
                                         const Surface& surface);

/// Every output of `config`: its outputs, then its hierarchy's (`HierarchyOutputs`).
/// @throws ConfigError as `HierarchyGrids`.
std::vector<OutputSpec> ConfigOutputs(const TerrainConfig& config,
                                      const std::optional<RasterGrid>& base,
                                      const Surface& surface);

/// The manifest of `grids` as JSON: the factor and shrink, and per grid its name, region, level,
/// parent index (null for the base), size, pixel size in metres, centre, bounds in surface
/// coordinates and the file of each product, relative to the output folder.
std::string HierarchyManifest(const HierarchySpec& hierarchy,
                              const std::vector<HierarchyGrid>& grids, const Surface& surface);

}  // namespace terraingen::config
