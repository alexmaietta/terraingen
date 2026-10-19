#include "terraingen/pipeline/nested.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace terraingen {

namespace {

/// The box of cell centres of a north-up grid.
Bounds2d CentreBox(const RasterGrid& grid) {
    const Vec2d first = grid.transform.PixelToWorld(0.5, 0.5);
    const Vec2d last = grid.transform.PixelToWorld(static_cast<double>(grid.width) - 0.5,
                                                   static_cast<double>(grid.height) - 0.5);
    return {std::min(first.x, last.x), std::min(first.y, last.y), std::max(first.x, last.x),
            std::max(first.y, last.y)};
}

/// Whether the open boxes `a` and `b` overlap.
bool Overlap(const Bounds2d& a, const Bounds2d& b) {
    return a.minX < b.maxX && b.minX < a.maxX && a.minY < b.maxY && b.minY < a.maxY;
}

/// `value` rounded to a whole number when within rounding of one, as cell positions on a coarser
/// lattice are.
double Snapped(double value) {
    const double nearest = std::round(value);
    return std::fabs(value - nearest) < 1e-9 ? nearest : value;
}

/// `heights` interpolated bilinearly at world (`x`, `y`) between cell centres; NaN outside them or
/// next to a cell without data.
double Bilinear(const Raster<float>& heights, double x, double y) {
    const Vec2d pixel = heights.Transform().WorldToPixel(x, y);
    const double u = Snapped(pixel.x - 0.5);
    const double v = Snapped(pixel.y - 0.5);
    const double col = std::floor(u);
    const double row = std::floor(v);
    const double fu = u - col;
    const double fv = v - row;
    const auto at = [&](double c, double r, double weight) {
        if (weight == 0.0) {
            return 0.0;
        }
        if (c < 0.0 || r < 0.0 || c >= static_cast<double>(heights.Width()) ||
            r >= static_cast<double>(heights.Height())) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const float value = heights(static_cast<std::size_t>(c), static_cast<std::size_t>(r));
        return heights.IsNoData(value) || std::isnan(value)
                   ? std::numeric_limits<double>::quiet_NaN()
                   : weight * static_cast<double>(value);
    };
    return at(col, row, (1.0 - fu) * (1.0 - fv)) + at(col + 1.0, row, fu * (1.0 - fv)) +
           at(col, row + 1.0, (1.0 - fu) * fv) + at(col + 1.0, row + 1.0, fu * fv);
}

/// Checks that `base` has square north-up cells.
void CheckBase(const RasterGrid& base) {
    const GeoTransform& t = base.transform;
    if (base.width == 0 || base.height == 0 || t.IsRotated() || !(t.pixelWidth > 0.0) ||
        !(t.pixelHeight < 0.0) ||
        std::fabs(t.pixelWidth + t.pixelHeight) > 1e-9 * std::fabs(t.pixelWidth)) {
        throw std::invalid_argument("A nested terrain needs a base grid of square north-up cells");
    }
}

}  // namespace

std::vector<NestedLevel> RepeatedLevels(double basePixelSize, double size, std::size_t count,
                                        std::size_t factor, double shrink) {
    RequirePositive(basePixelSize, "Base pixel size");
    RequirePositive(size, "Level size");
    if (factor < 2) {
        throw std::invalid_argument("Nested levels need a factor of at least 2");
    }
    if (!(shrink >= 1.0) || !std::isfinite(shrink)) {
        throw std::invalid_argument("Nested levels need a finite shrink of at least 1");
    }
    std::vector<NestedLevel> levels;
    double pixelSize = basePixelSize;
    for (std::size_t k = 0; k < count; ++k) {
        pixelSize /= static_cast<double>(factor);
        levels.push_back({pixelSize, size});
        size /= shrink;
    }
    return levels;
}

std::vector<NestedGrid> NestGrids(const RasterGrid& base, const std::vector<NestedRegion>& regions,
                                  const Surface& surface) {
    CheckBase(base);
    std::vector<NestedGrid> grids;
    grids.push_back({std::nullopt, 0, base, std::nullopt, {}});
    const Vec2d origin = base.transform.PixelToWorld(0.5, 0.5);
    const double baseStep = base.transform.pixelWidth;
    const double metresPerUnit = surface.UnitLength();
    std::vector<Bounds2d> squares;
    for (std::size_t r = 0; r < regions.size(); ++r) {
        const NestedRegion& region = regions[r];
        const auto fail = [&](const std::string& message) {
            throw NestedRegionError(
                r, "Region " +
                       (region.name.empty() ? std::to_string(r) : "\"" + region.name + "\"") + " " +
                       message);
        };
        if (region.name.empty()) {
            fail("needs a name");
        }
        for (std::size_t other = 0; other < r; ++other) {
            if (regions[other].name == region.name) {
                fail("has the name of another region");
            }
        }
        if (region.levels.empty()) {
            fail("needs a level");
        }
        if (!std::isfinite(region.centre.x) || !std::isfinite(region.centre.y)) {
            fail("needs a finite centre");
        }
        const Vec2d centre{
            origin.x + std::round((region.centre.x - origin.x) / baseStep) * baseStep,
            origin.y + std::round((region.centre.y - origin.y) / baseStep) * baseStep};
        std::size_t parent = 0;
        double parentPixel = baseStep * metresPerUnit;
        double parentStep = baseStep;
        for (std::size_t level = 1; level <= region.levels.size(); ++level) {
            const NestedLevel& spec = region.levels[level - 1];
            const std::string which = "level " + std::to_string(level);
            if (!(spec.pixelSize > 0.0) || !std::isfinite(spec.pixelSize) || !(spec.size > 0.0) ||
                !std::isfinite(spec.size)) {
                fail(which + " needs a positive, finite pixel size and size");
            }
            const double ratio = std::round(parentPixel / spec.pixelSize);
            if (ratio < 2.0 || std::fabs(parentPixel / spec.pixelSize - ratio) > 1e-6 * ratio) {
                fail(which + " needs a pixel size of the level above's over a whole number of at "
                             "least 2");
            }
            const double step = parentStep / ratio;
            const double half =
                std::max(1.0, std::round(spec.size / (2.0 * ratio * spec.pixelSize)));
            const double cells = 2.0 * half * ratio + 1.0;
            if (cells > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
                fail(which + " has too many cells");
            }
            NestedGrid grid;
            grid.region = r;
            grid.level = level;
            grid.parent = parent;
            grid.grid = {static_cast<std::size_t>(cells), static_cast<std::size_t>(cells),
                         GeoTransform::NorthUp(centre.x - 0.5 * cells * step,
                                               centre.y + 0.5 * cells * step, step)};
            const Bounds2d box = CentreBox(grid.grid);
            const Bounds2d outer = CentreBox(grids[parent].grid);
            const double margin = parentStep * (1.0 - 1e-9);
            if (box.minX < outer.minX + margin || box.maxX > outer.maxX - margin ||
                box.minY < outer.minY + margin || box.maxY > outer.maxY - margin) {
                fail(which + " does not fit inside the level above");
            }
            if (level == 1) {
                const Bounds2d square = grid.grid.Bounds();
                for (std::size_t other = 0; other < squares.size(); ++other) {
                    if (Overlap(square, squares[other])) {
                        fail("overlaps region \"" + regions[other].name + "\"");
                    }
                }
                squares.push_back(square);
            }
            grids[parent].children.push_back(grids.size());
            parent = grids.size();
            parentPixel = spec.pixelSize;
            parentStep = step;
            grids.push_back(grid);
        }
    }
    return grids;
}

NestedTerrain::NestedTerrain(Terrain terrain, RasterGrid base, std::vector<NestedRegion> regions)
    : terrain_(std::move(terrain)), regions_(std::move(regions)),
      grids_(NestGrids(base, regions_, terrain_.surface)) {
    ValidateTerrain(terrain_);
    for (const NestedRegion& region : regions_) {
        Terrain refined;
        refined.source = terrain_.source;
        refined.surface = terrain_.surface;
        for (const auto& layer : region.upscaling) {
            if (!layer) {
                throw std::invalid_argument("Region \"" + region.name +
                                            "\" has a null upscaling layer");
            }
            refined.layers.push_back(layer);
        }
        refined.layers.insert(refined.layers.end(), terrain_.layers.begin(), terrain_.layers.end());
        regionTerrains_.push_back(std::move(refined));
    }
}

std::optional<std::size_t> NestedTerrain::FindRegion(std::string_view name) const {
    for (std::size_t r = 0; r < regions_.size(); ++r) {
        if (regions_[r].name == name) {
            return r;
        }
    }
    return std::nullopt;
}

std::size_t NestedTerrain::GridIndex(std::size_t region, std::size_t level) const {
    if (region >= regions_.size() || level > regions_[region].levels.size()) {
        throw std::out_of_range("No level " + std::to_string(level) + " of region " +
                                std::to_string(region));
    }
    if (level == 0) {
        return 0;
    }
    for (std::size_t g = 0; g < grids_.size(); ++g) {
        if (grids_[g].region == region && grids_[g].level == level) {
            return g;
        }
    }
    throw std::out_of_range("No level " + std::to_string(level));
}

const Terrain& NestedTerrain::GridTerrain(std::size_t grid) const {
    const std::optional<std::size_t> region = grids_.at(grid).region;
    return region ? regionTerrains_[*region] : terrain_;
}

Raster<float> NestedTerrain::Evaluate(std::size_t grid, const Raster<float>* parent,
                                      const EvaluationOptions& options) const {
    Raster<float> heights = terraingen::Evaluate(GridTerrain(grid), grids_.at(grid).grid, options);
    if (parent && grids_[grid].parent) {
        BlendEdges(grid, *parent, heights);
    }
    return heights;
}

std::vector<Raster<float>> NestedTerrain::EvaluateAll(const EvaluationOptions& options) const {
    std::vector<Raster<float>> heights;
    heights.reserve(grids_.size());
    for (std::size_t g = 0; g < grids_.size(); ++g) {
        const std::optional<std::size_t> parent = grids_[g].parent;
        heights.push_back(Evaluate(g, parent ? &heights[*parent] : nullptr, options));
    }
    return heights;
}

void NestedTerrain::BlendEdges(std::size_t grid, const Raster<float>& parent,
                               Raster<float>& heights) const {
    const std::optional<std::size_t> region = grids_.at(grid).region;
    const std::size_t cells = region ? regions_[*region].blendCells : 0;
    const std::size_t width = heights.Width();
    const std::size_t height = heights.Height();
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            const std::size_t edge =
                std::min(std::min(col, row), std::min(width - 1 - col, height - 1 - row));
            if (edge >= cells || heights.IsNoData(heights(col, row))) {
                continue;
            }
            const Vec2d centre = heights.CellCenter(col, row);
            const double outer = Bilinear(parent, centre.x, centre.y);
            if (std::isnan(outer)) {
                continue;
            }
            const double t = static_cast<double>(edge) / static_cast<double>(cells);
            const double weight = t * t * (3.0 - 2.0 * t);
            heights(col, row) = static_cast<float>(
                outer + weight * (static_cast<double>(heights(col, row)) - outer));
        }
    }
}

HeightMeshOptions NestedTerrain::MeshOptions(std::size_t grid, double skirt) const {
    HeightMeshOptions options;
    options.skirt = skirt;
    for (std::size_t child : grids_.at(grid).children) {
        const Bounds2d box = CentreBox(grids_[child].grid);
        const double pad = 1e-6 * grids_[child].grid.transform.pixelWidth;
        options.cutouts.push_back({box.minX - pad, box.minY - pad, box.maxX + pad, box.maxY + pad});
    }
    return options;
}

TriangleMesh NestedTerrain::Mesh(const std::vector<Raster<float>>& heights,
                                 const std::vector<std::size_t>& grids, double skirt) const {
    if (heights.size() != grids_.size()) {
        throw std::invalid_argument("A nested mesh needs heights over every grid");
    }
    TriangleMesh merged;
    for (std::size_t g : grids) {
        if (g >= grids_.size()) {
            throw std::invalid_argument("A nested terrain has no grid " + std::to_string(g));
        }
        if (RasterGrid::FromRaster(heights[g]) != grids_[g].grid) {
            throw std::invalid_argument("The heights of grid " + std::to_string(g) +
                                        " are not over it");
        }
        HeightMeshOptions options;
        options.skirt = skirt;
        const HeightMeshOptions all = MeshOptions(g, skirt);
        for (std::size_t c = 0; c < grids_[g].children.size(); ++c) {
            if (std::find(grids.begin(), grids.end(), grids_[g].children[c]) != grids.end()) {
                options.cutouts.push_back(all.cutouts[c]);
            }
        }
        const TriangleMesh mesh = HeightMesh(heights[g], terrain_.surface, Site(), options);
        const auto offset = static_cast<std::uint32_t>(merged.positions.size());
        merged.positions.insert(merged.positions.end(), mesh.positions.begin(),
                                mesh.positions.end());
        for (const auto& triangle : mesh.triangles) {
            merged.triangles.push_back(
                {triangle[0] + offset, triangle[1] + offset, triangle[2] + offset});
        }
    }
    return merged;
}

Vec2d NestedTerrain::Site() const {
    const Bounds2d bounds = Base().Bounds();
    return {0.5 * (bounds.minX + bounds.maxX), 0.5 * (bounds.minY + bounds.maxY)};
}

}  // namespace terraingen
