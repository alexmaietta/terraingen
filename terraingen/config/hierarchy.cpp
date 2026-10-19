#include "terraingen/config/hierarchy.h"

#include "terraingen/config/build.h"
#include "terraingen/config/fields/checks.h"
#include "terraingen/config/fields/enums.h"
#include "terraingen/config/json.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace terraingen::config {

namespace {

constexpr std::string_view kRegionKey = "{region}";
constexpr std::string_view kLevelKey = "{level}";

/// `text` with every `key` replaced by `value`.
std::string Replace(std::string text, std::string_view key, const std::string& value) {
    for (std::size_t at = text.find(key); at != std::string::npos;
         at = text.find(key, at + value.size())) {
        text.replace(at, key.size(), value);
    }
    return text;
}

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

/// Level 0: the hierarchy's base, or else the base's grid.
GridRegionSpec BaseLevel(const HierarchySpec& hierarchy, const std::optional<RasterGrid>& base,
                         const Surface& surface) {
    if (hierarchy.base) {
        return *hierarchy.base;
    }
    if (!base) {
        throw ConfigError("hierarchy.base", "required without a base");
    }
    const GeoTransform& transform = base->transform;
    if (transform.IsRotated() ||
        std::fabs(std::fabs(transform.pixelWidth) - std::fabs(transform.pixelHeight)) >
            1e-9 * std::fabs(transform.pixelWidth)) {
        throw ConfigError("hierarchy.base", "required for a base without square, north-up cells");
    }
    const Bounds2d bounds = base->Bounds();
    GridRegionSpec level;
    level.width = base->width;
    level.height = base->height;
    level.pixelSize = std::fabs(transform.pixelHeight) * surface.UnitLength();
    level.centreX = 0.5 * (bounds.minX + bounds.maxX);
    level.centreY = 0.5 * (bounds.minY + bounds.maxY);
    return level;
}

/// The suffix and extension of `product`'s files.
std::string ProductSuffix(HierarchyProduct product, HierarchyMeshFormat format) {
    static constexpr std::array<std::string_view, 4> kMeshExtensions{".glb", ".gltf", ".obj",
                                                                     ".stl"};
    const std::string mesh(kMeshExtensions[static_cast<std::size_t>(format)]);
    switch (product) {
    case HierarchyProduct::Heightmap:
        return ".tif";
    case HierarchyProduct::Albedo:
        return "_albedo.tif";
    case HierarchyProduct::Materials:
        return "_materials.tif";
    case HierarchyProduct::Photometry:
        return "_photometry.tif";
    case HierarchyProduct::TerrainMesh:
        return mesh;
    case HierarchyProduct::CraterList:
        return "_craters.csv";
    case HierarchyProduct::BoulderList:
        return "_boulders.csv";
    case HierarchyProduct::BoulderModels:
        return "_boulders" + mesh;
    }
    return {};
}

/// The JSON name of `product`.
std::string ProductName(HierarchyProduct product) {
    for (const auto& entry : EnumNames<HierarchyProduct>::kValues) {
        if (entry.value == product) {
            return std::string(entry.name);
        }
    }
    return {};
}

/// An output of `Output`'s kind to `path` over `region`.
template <typename Output> Output Covering(const std::string& path, const GridRegionSpec& region) {
    Output output;
    output.path = path;
    output.region = region;
    return output;
}

}  // namespace

std::vector<HierarchyGrid> HierarchyGrids(const HierarchySpec& hierarchy,
                                          const std::optional<RasterGrid>& base,
                                          const Surface& surface) {
    if (hierarchy.path.find(kRegionKey) == std::string::npos ||
        hierarchy.path.find(kLevelKey) == std::string::npos) {
        throw ConfigError("hierarchy.path", "must hold {region} and {level}");
    }
    std::vector<HierarchyGrid> grids;
    HierarchyGrid root;
    root.grid = BaseLevel(hierarchy, base, surface);
    grids.push_back(root);
    const RasterGrid baseGrid = RegionGrid(root.grid, surface);
    const Vec2d origin = baseGrid.transform.PixelToWorld(0.5, 0.5);
    const double baseStep = std::fabs(baseGrid.transform.pixelWidth);
    const auto factor = static_cast<double>(hierarchy.factor);
    std::vector<Bounds2d> squares;
    for (std::size_t r = 0; r < hierarchy.regions.size(); ++r) {
        const HierarchyRegionSpec& region = hierarchy.regions[r];
        const std::string path = Element("hierarchy.regions", r);
        const Vec2d centre{origin.x + std::round((region.centreX - origin.x) / baseStep) * baseStep,
                           origin.y +
                               std::round((region.centreY - origin.y) / baseStep) * baseStep};
        std::size_t parent = 0;
        double pixelSize = root.grid.pixelSize;
        double side = region.size;
        for (std::size_t level = 1; level <= region.levels; ++level) {
            pixelSize /= factor;
            if (level > 1) {
                side /= hierarchy.shrink;
            }
            const double half = std::max(1.0, std::round(side / (2.0 * factor * pixelSize)));
            const double cells = 2.0 * half * factor + 1.0;
            if (cells > static_cast<double>(kMaxConfigGridSize)) {
                throw ConfigError(path, "level " + std::to_string(level) + " needs more than " +
                                            std::to_string(kMaxConfigGridSize) + " cells a side");
            }
            HierarchyGrid grid;
            grid.region = r;
            grid.level = level;
            grid.parent = parent;
            grid.grid = {static_cast<std::size_t>(cells), static_cast<std::size_t>(cells),
                         pixelSize, centre.x, centre.y};
            const Bounds2d box = CentreBox(RegionGrid(grid.grid, surface));
            const Bounds2d outer = CentreBox(RegionGrid(grids[parent].grid, surface));
            const double margin =
                std::fabs(RegionGrid(grids[parent].grid, surface).transform.pixelWidth) *
                (1.0 - 1e-9);
            if (box.minX < outer.minX + margin || box.maxX > outer.maxX - margin ||
                box.minY < outer.minY + margin || box.maxY > outer.maxY - margin) {
                throw ConfigError(path, "level " + std::to_string(level) +
                                            " does not fit inside the level it lies in");
            }
            if (level == 1) {
                const Bounds2d square = RegionGrid(grid.grid, surface).Bounds();
                for (std::size_t other = 0; other < squares.size(); ++other) {
                    if (Overlap(square, squares[other])) {
                        throw ConfigError(path, "overlaps hierarchy.regions[" +
                                                    std::to_string(other) + "]");
                    }
                }
                squares.push_back(square);
            }
            grids[parent].children.push_back(grids.size());
            parent = grids.size();
            grids.push_back(grid);
        }
    }
    for (HierarchyGrid& grid : grids) {
        grid.name = Replace(Replace(hierarchy.path, kRegionKey, std::to_string(grid.region)),
                            kLevelKey, std::to_string(grid.level));
    }
    return grids;
}

std::string HierarchyFile(const HierarchySpec& hierarchy, const HierarchyGrid& grid,
                          HierarchyProduct product) {
    return grid.name + ProductSuffix(product, hierarchy.meshFormat);
}

std::vector<OutputSpec> HierarchyOutputs(const HierarchySpec& hierarchy,
                                         const std::vector<HierarchyGrid>& grids,
                                         const Surface& surface) {
    std::vector<OutputSpec> outputs;
    for (const HierarchyGrid& grid : grids) {
        for (HierarchyProduct product : hierarchy.products) {
            const std::string file = HierarchyFile(hierarchy, grid, product);
            switch (product) {
            case HierarchyProduct::Heightmap:
                outputs.push_back(Covering<HeightmapOutput>(file, grid.grid));
                break;
            case HierarchyProduct::Albedo:
                outputs.push_back(Covering<AlbedoOutput>(file, grid.grid));
                break;
            case HierarchyProduct::Materials:
                outputs.push_back(Covering<MaterialsOutput>(file, grid.grid));
                break;
            case HierarchyProduct::Photometry:
                outputs.push_back(Covering<PhotometryOutput>(file, grid.grid));
                break;
            case HierarchyProduct::TerrainMesh: {
                TerrainMeshOutput mesh;
                mesh.path = file;
                mesh.region = grid.grid;
                mesh.skirt = hierarchy.skirt;
                mesh.siteX = grids[0].grid.centreX;
                mesh.siteY = grids[0].grid.centreY;
                for (std::size_t child : grid.children) {
                    const RasterGrid cells = RegionGrid(grids[child].grid, surface);
                    const Bounds2d box = CentreBox(cells);
                    const double pad = 1e-6 * std::fabs(cells.transform.pixelWidth);
                    mesh.cutouts.push_back(
                        {box.minX - pad, box.minY - pad, box.maxX + pad, box.maxY + pad});
                }
                outputs.push_back(mesh);
                break;
            }
            case HierarchyProduct::CraterList:
                outputs.push_back(Covering<CraterListOutput>(file, grid.grid));
                break;
            case HierarchyProduct::BoulderList:
                outputs.push_back(Covering<BoulderListOutput>(file, grid.grid));
                break;
            case HierarchyProduct::BoulderModels: {
                BoulderModelsOutput models;
                models.path = file;
                models.region = grid.grid;
                outputs.push_back(models);
                break;
            }
            }
        }
    }
    return outputs;
}

std::vector<OutputSpec> ConfigOutputs(const TerrainConfig& config,
                                      const std::optional<RasterGrid>& base,
                                      const Surface& surface) {
    std::vector<OutputSpec> outputs = config.outputs;
    if (config.hierarchy) {
        const std::vector<OutputSpec> extra = HierarchyOutputs(
            *config.hierarchy, HierarchyGrids(*config.hierarchy, base, surface), surface);
        outputs.insert(outputs.end(), extra.begin(), extra.end());
    }
    return outputs;
}

std::string HierarchyManifest(const HierarchySpec& hierarchy,
                              const std::vector<HierarchyGrid>& grids, const Surface& surface) {
    nlohmann::ordered_json manifest;
    manifest["factor"] = hierarchy.factor;
    manifest["shrink"] = hierarchy.shrink;
    nlohmann::ordered_json list = nlohmann::ordered_json::array();
    for (const HierarchyGrid& grid : grids) {
        const Bounds2d bounds = RegionGrid(grid.grid, surface).Bounds();
        nlohmann::ordered_json entry;
        entry["name"] = grid.name;
        entry["region"] = grid.region;
        entry["level"] = grid.level;
        entry["parent"] = grid.parent ? nlohmann::ordered_json(*grid.parent) : nullptr;
        entry["width"] = grid.grid.width;
        entry["height"] = grid.grid.height;
        entry["pixel_size"] = grid.grid.pixelSize;
        entry["centre_x"] = grid.grid.centreX;
        entry["centre_y"] = grid.grid.centreY;
        entry["bounds"] = {bounds.minX, bounds.minY, bounds.maxX, bounds.maxY};
        nlohmann::ordered_json files = nlohmann::ordered_json::object();
        for (HierarchyProduct product : hierarchy.products) {
            files[ProductName(product)] = HierarchyFile(hierarchy, grid, product);
        }
        entry["files"] = files;
        list.push_back(entry);
    }
    manifest["grids"] = list;
    return manifest.dump(2) + "\n";
}

}  // namespace terraingen::config
