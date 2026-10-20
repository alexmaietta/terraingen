/// @file
/// @brief A configuration's regions laid out as nested terrains, and its outputs resolved to the
/// grids and terrains they cover.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/pipeline/nested.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/surface.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace terraingen::config {

/// The levels of `region` below a base of `basePixelSize` metres: its `levels`, or its `repeat`.
std::vector<NestedLevel> RegionLevels(const RegionSpec& region, double basePixelSize);

/// The number of levels of `region` below its base.
std::size_t RegionLevelCount(const RegionSpec& region);

/// A configuration's regions grouped by base grid, each group the grids of one nested terrain.
struct RegionLayout {
    /// Regions sharing a base.
    struct Group {
        RasterGrid base{};
        std::vector<std::size_t> regions;  ///< Indices of the configuration's regions.
        /// As `NestGrids` over them; a grid's region is its index in `regions`.
        std::vector<NestedGrid> grids;
    };

    std::vector<Group> groups;
    std::vector<std::string> names;   ///< Of each of the configuration's regions.
    std::vector<std::size_t> group;   ///< Of each region, its group.
    std::vector<std::size_t> member;  ///< Of each region, its index in its group.

    /// The index of the region named `name`, if any.
    std::optional<std::size_t> Find(std::string_view name) const;

    /// The index in its group's grids of `level` of region `region`; level 0 is the base.
    std::size_t GridIndex(std::size_t region, std::size_t level) const;
};

/// The regions grouped by base, in order of first use, with their grids.
/// @param base The base's grid, every region's level 0 unless it gives its own.
/// @throws ConfigError at "regions[i]" for a region with both or neither of levels and repeat, or
/// for one `NestGrids` rejects, or at "regions[i].base" for no base grid.
RegionLayout LayOutRegions(const std::vector<RegionSpec>& regions,
                           const std::optional<RasterGrid>& base, const Surface& surface);

/// An output and what it covers.
struct ResolvedOutput {
    OutputSpec spec;    ///< With "{region}" and "{level}" in its path replaced.
    std::string where;  ///< Its JSON path, such as "outputs[2]".
    /// The grid it covers; none for a mesh, a nested mesh or a manifest.
    std::optional<RasterGrid> grid{};
    /// The region whose terrain evaluates it, for a region's levels from 1; none for the terrain.
    std::optional<std::size_t> region{};
    std::optional<std::size_t> group{};  ///< The group of the nested grids it covers, if any.
    std::vector<std::size_t> grids{};    ///< Their indices in the group's grids.
};

/// Every output of `config` resolved: one per level for an output of every level of a region, the
/// rest as they are.
/// @throws ConfigError for an output giving both a region and a nested level, naming an unknown
/// region or level, covering every level without "{level}" in its path, or a nested mesh without
/// regions, with regions on different bases or several regions and "{level}" without "{region}".
std::vector<ResolvedOutput> ResolveOutputs(const TerrainConfig& config,
                                           const std::optional<RasterGrid>& base,
                                           const Surface& surface, const RegionLayout& layout);

/// The file of a nested mesh output written for grid `grid` of its group: its path with the
/// grid's region (the first region for the base) and level in.
std::string NestedMeshFile(const NestedMeshOutput& output, const RegionLayout& layout,
                           std::size_t group, std::size_t grid);

/// Whether a nested mesh output writes one file per grid rather than one merged file.
bool SplitsNestedMesh(const NestedMeshOutput& output);

/// The manifest `manifest` writes, as JSON: per grid of the groups of its regions, the group, its
/// index there, its region and level (null region for a base), its parent index, size, pixel size
/// in metres, centre and bounds in surface coordinates, and the files `outputs` write over it,
/// by output type and relative to the output folder.
std::string RegionManifest(const ManifestOutput& manifest, const RegionLayout& layout,
                           const std::vector<ResolvedOutput>& outputs, const Surface& surface);

}  // namespace terraingen::config
