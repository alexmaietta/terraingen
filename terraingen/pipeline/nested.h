/// @file
/// @brief Nested terrains: squares refined level by level inside a base grid, for multi-resolution
/// heightmaps and meshes.
#pragma once

#include "terraingen/mesh/mesh.h"
#include "terraingen/pipeline/height_mesh.h"
#include "terraingen/pipeline/terrain.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace terraingen {

/// One level of a nested region: square cells of `pixelSize` metres across a square about `size`
/// metres wide.
struct NestedLevel {
    double pixelSize = 0.0;  ///< Metres; the level above's over a whole number of at least 2.
    double size = 0.0;       ///< Metres; rounded to cells that end on the level above's centres.

    bool operator==(const NestedLevel& other) const {
        return pixelSize == other.pixelSize && size == other.size;
    }
    bool operator!=(const NestedLevel& other) const {
        return !(*this == other);
    }
};

/// `count` levels below a base of `basePixelSize` metres: level k has cells of basePixelSize /
/// factor^k and is `size` / shrink^(k - 1) metres across.
/// @throws std::invalid_argument for a `basePixelSize` or `size` that is not positive and finite, a
/// `factor` below 2 or a `shrink` below 1.
std::vector<NestedLevel> RepeatedLevels(double basePixelSize, double size, std::size_t count,
                                        std::size_t factor = 2, double shrink = 2.0);

/// A square of terrain refined level by level about `centre` inside a base grid, the base being
/// its level 0.
struct NestedRegion {
    std::string name;  ///< Unique among the regions of a nested terrain.
    Vec2d centre{};    ///< Surface coordinates; snapped to the base's nearest cell centre.
    std::vector<NestedLevel> levels;  ///< Level 1 first, each finer than the one above.
    /// Layers that add the detail of the finer levels, such as midpoint displacement, applied to
    /// every level after the source and before the terrain's layers; the base has none, so they
    /// should add nothing at the base's spacing.
    std::vector<std::shared_ptr<const Layer>> upscaling;
    /// Cells along a level's edges over which its heights blend from the level above's, which
    /// they equal on the edge; 0 keeps the level's own heights.
    std::size_t blendCells = 8;
};

/// A grid of a nested terrain.
struct NestedGrid {
    std::optional<std::size_t> region;    ///< Index of its region; none for the base.
    std::size_t level = 0;                ///< 0 for the base.
    RasterGrid grid{};                    ///< North-up square cells.
    std::optional<std::size_t> parent;    ///< Index of the grid it lies in; none for the base.
    std::vector<std::size_t> children{};  ///< Indices of the grids that lie in it.
};

/// The problem with one region of a nested terrain.
class NestedRegionError : public std::invalid_argument {
  public:
    NestedRegionError(std::size_t region, const std::string& message)
        : std::invalid_argument(message), region_(region) {}

    /// Index of the region.
    std::size_t Region() const {
        return region_;
    }

  private:
    std::size_t region_;
};

/// The grids of `regions` nested in `base`: the base first, then each region's levels from coarse
/// to fine.
///
/// Each level's cell centres lie on the level above's refined by the ratio of their pixel sizes,
/// an odd number across whose edge rows and columns lie on the level above's cell centres, with
/// at least one cell of the level above beyond them on each side. So a level holds the level
/// above's cells at every ratio-th cell, and its edges meet the level above's mesh.
/// @throws std::invalid_argument for a base without square north-up cells.
/// @throws NestedRegionError for a region without a name, with the name of one before it, with no
/// levels or a non-finite centre, with a level whose pixel size or size is not positive and finite,
/// whose pixel size is not the level above's over a whole number of at least 2, or that does not
/// fit in the level above, or whose level 1 overlaps another region's.
std::vector<NestedGrid> NestGrids(const RasterGrid& base, const std::vector<NestedRegion>& regions,
                                  const Surface& surface = {});

/// A terrain refined in nested regions: the base grid evaluated with the terrain, and each region's
/// levels with the region's upscaling layers too, every level blended into the one above along its
/// edges.
class NestedTerrain {
  public:
    /// @throws std::invalid_argument as `NestGrids`, or for a null upscaling layer.
    NestedTerrain(Terrain terrain, RasterGrid base, std::vector<NestedRegion> regions);

    /// The base grid, level 0.
    const RasterGrid& Base() const {
        return grids_.front().grid;
    }

    /// Every grid, as `NestGrids` orders them.
    const std::vector<NestedGrid>& Grids() const {
        return grids_;
    }

    /// The regions.
    const std::vector<NestedRegion>& Regions() const {
        return regions_;
    }

    /// The index of the region named `name`, if any.
    std::optional<std::size_t> FindRegion(std::string_view name) const;

    /// The index in `Grids` of `level` of region `region`; level 0 is the base.
    /// @throws std::out_of_range for a region or level that does not exist.
    std::size_t GridIndex(std::size_t region, std::size_t level) const;

    /// The terrain grid `grid` is evaluated with: the terrain for the base, and for a region's
    /// levels its source, the region's upscaling layers and then the terrain's layers.
    const Terrain& GridTerrain(std::size_t grid) const;

    /// The heights over grid `grid`, its edges blended into `parent`, the heights over the grid it
    /// lies in; `parent` is unused for the base.
    Raster<float> Evaluate(std::size_t grid, const Raster<float>* parent,
                           const EvaluationOptions& options = {}) const;

    /// The heights over every grid, in the order of `Grids`.
    std::vector<Raster<float>> EvaluateAll(const EvaluationOptions& options = {}) const;

    /// Blends `heights` over grid `grid` into `parent`, the heights over the grid it lies in, over
    /// the region's `blendCells` cells along its edges with a smoothstep from `parent` on the edge;
    /// cells where `parent` has no data keep their own.
    void BlendEdges(std::size_t grid, const Raster<float>& parent, Raster<float>& heights) const;

    /// What the mesh of grid `grid` leaves out: its children's cells, edges included, with a skirt
    /// `skirt` metres deep.
    HeightMeshOptions MeshOptions(std::size_t grid, double skirt = 0.0) const;

    /// The meshes of grids `grids` merged into one, each cut out under those of its children that
    /// are among `grids`, in the frame of the base's centre (`HeightMesh`).
    /// @param heights The heights over every grid, in the order of `Grids`.
    /// @throws std::invalid_argument for heights not over the grids, or a grid index out of range.
    TriangleMesh Mesh(const std::vector<Raster<float>>& heights,
                      const std::vector<std::size_t>& grids, double skirt = 0.0) const;

    /// The centre of the base, the site meshes are framed at.
    Vec2d Site() const;

  private:
    Terrain terrain_;
    std::vector<NestedRegion> regions_;
    std::vector<NestedGrid> grids_;
    std::vector<Terrain> regionTerrains_;
};

}  // namespace terraingen
