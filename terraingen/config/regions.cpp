#include "terraingen/config/regions.h"

#include "terraingen/config/build.h"
#include "terraingen/config/fields/checks.h"
#include "terraingen/config/fields/fields.h"
#include "terraingen/config/json.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <type_traits>
#include <utility>
#include <variant>

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

bool Holds(const std::string& text, std::string_view key) {
    return text.find(key) != std::string::npos;
}

/// Whether an output of type `T` can cover a region's level.
template <typename T, typename = void> struct CoversLevels : std::false_type {};
template <typename T>
struct CoversLevels<T, std::void_t<decltype(std::declval<T&>().nested)>> : std::true_type {};

/// The box of cell centres of a north-up grid, padded to hold them on its edges.
RectangleRegionSpec CentreBox(const RasterGrid& grid) {
    const Vec2d first = grid.transform.PixelToWorld(0.5, 0.5);
    const Vec2d last = grid.transform.PixelToWorld(static_cast<double>(grid.width) - 0.5,
                                                   static_cast<double>(grid.height) - 0.5);
    const double pad = 1e-6 * std::fabs(grid.transform.pixelWidth);
    return {std::min(first.x, last.x) - pad, std::min(first.y, last.y) - pad,
            std::max(first.x, last.x) + pad, std::max(first.y, last.y) + pad};
}

/// The centre of a grid's extent.
Vec2d Centre(const RasterGrid& grid) {
    const Bounds2d bounds = grid.Bounds();
    return {0.5 * (bounds.minX + bounds.maxX), 0.5 * (bounds.minY + bounds.maxY)};
}

/// The JSON type name of an output.
std::string OutputType(const OutputSpec& output) {
    return std::visit(
        [](const auto& o) { return std::string(SpecName<std::decay_t<decltype(o)>>::kName); },
        output);
}

}  // namespace

std::vector<NestedLevel> RegionLevels(const RegionSpec& region, double basePixelSize) {
    if (region.repeat) {
        const RepeatedLevelsSpec& repeat = *region.repeat;
        return RepeatedLevels(basePixelSize, repeat.size, repeat.count, repeat.factor,
                              repeat.shrink);
    }
    std::vector<NestedLevel> levels;
    for (const RegionLevelSpec& level : region.levels) {
        levels.push_back({level.pixelSize, level.size});
    }
    return levels;
}

std::size_t RegionLevelCount(const RegionSpec& region) {
    return region.repeat ? region.repeat->count : region.levels.size();
}

std::optional<std::size_t> RegionLayout::Find(std::string_view name) const {
    const auto found = std::find(names.begin(), names.end(), name);
    return found == names.end()
               ? std::nullopt
               : std::optional<std::size_t>(static_cast<std::size_t>(found - names.begin()));
}

std::size_t RegionLayout::GridIndex(std::size_t region, std::size_t level) const {
    if (level == 0) {
        return 0;
    }
    const Group& owner = groups.at(group.at(region));
    for (std::size_t g = 0; g < owner.grids.size(); ++g) {
        if (owner.grids[g].region == member[region] && owner.grids[g].level == level) {
            return g;
        }
    }
    throw std::out_of_range("Region " + names[region] + " has no level " + std::to_string(level));
}

RegionLayout LayOutRegions(const std::vector<RegionSpec>& regions,
                           const std::optional<RasterGrid>& base, const Surface& surface) {
    RegionLayout layout;
    for (std::size_t r = 0; r < regions.size(); ++r) {
        const RegionSpec& region = regions[r];
        const std::string path = Element("regions", r);
        if (region.repeat.has_value() == !region.levels.empty()) {
            throw ConfigError(path, "needs levels or repeat, one of the two");
        }
        if (!region.base && !base) {
            throw ConfigError(Child(path, "base"), "required without a base");
        }
        const RasterGrid grid = region.base ? RegionGrid(*region.base, surface) : *base;
        auto found = std::find_if(layout.groups.begin(), layout.groups.end(),
                                  [&](const RegionLayout::Group& g) { return g.base == grid; });
        if (found == layout.groups.end()) {
            layout.groups.push_back({grid, {}, {}});
            found = layout.groups.end() - 1;
        }
        layout.names.push_back(region.name);
        layout.group.push_back(static_cast<std::size_t>(found - layout.groups.begin()));
        layout.member.push_back(found->regions.size());
        found->regions.push_back(r);
    }
    for (RegionLayout::Group& group : layout.groups) {
        const double basePixel = std::fabs(group.base.transform.pixelHeight) * surface.UnitLength();
        std::vector<NestedRegion> nested;
        for (std::size_t r : group.regions) {
            NestedRegion region;
            region.name = regions[r].name;
            region.centre = {regions[r].centreX, regions[r].centreY};
            try {
                region.levels = RegionLevels(regions[r], basePixel);
            } catch (const std::invalid_argument& error) {
                throw ConfigError(Element("regions", r), error.what());
            }
            nested.push_back(std::move(region));
        }
        try {
            group.grids = NestGrids(group.base, nested, surface);
        } catch (const NestedRegionError& error) {
            throw ConfigError(Element("regions", group.regions[error.Region()]), error.what());
        } catch (const std::invalid_argument& error) {
            throw ConfigError(Child(Element("regions", group.regions.front()), "base"),
                              error.what());
        }
    }
    return layout;
}

std::vector<ResolvedOutput> ResolveOutputs(const TerrainConfig& config,
                                           const std::optional<RasterGrid>& base,
                                           const Surface& surface, const RegionLayout& layout) {
    std::vector<ResolvedOutput> resolved;
    const auto region = [&](const std::string& name, const std::string& where) {
        const std::optional<std::size_t> found = layout.Find(name);
        if (!found) {
            throw ConfigError(where, "names no region");
        }
        return *found;
    };
    for (std::size_t i = 0; i < config.outputs.size(); ++i) {
        const OutputSpec& output = config.outputs[i];
        const std::string where = Element("outputs", i);
        std::visit(
            [&](const auto& o) {
                using Output = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<Output, NestedMeshOutput>) {
                    ResolvedOutput entry{output, where};
                    if (o.regions.empty()) {
                        throw ConfigError(Child(where, "regions"), "needs at least one region");
                    }
                    std::vector<std::size_t> grids{0};
                    for (std::size_t j = 0; j < o.regions.size(); ++j) {
                        const std::string at = Element(Child(where, "regions"), j);
                        const std::size_t r = region(o.regions[j], at);
                        if (entry.group && *entry.group != layout.group[r]) {
                            throw ConfigError(at, "lies in another base than " + o.regions[0]);
                        }
                        entry.group = layout.group[r];
                        const RegionLayout::Group& group = layout.groups[layout.group[r]];
                        for (std::size_t g = 1; g < group.grids.size(); ++g) {
                            if (group.grids[g].region == layout.member[r]) {
                                grids.push_back(g);
                            }
                        }
                    }
                    if (SplitsNestedMesh(o) && o.regions.size() > 1 && !Holds(o.path, kRegionKey)) {
                        throw ConfigError(Child(where, "path"),
                                          "needs {region} with {level} for several regions");
                    }
                    entry.grids = std::move(grids);
                    resolved.push_back(std::move(entry));
                } else if constexpr (std::is_same_v<Output, ManifestOutput>) {
                    for (std::size_t j = 0; j < o.regions.size(); ++j) {
                        region(o.regions[j], Element(Child(where, "regions"), j));
                    }
                    resolved.push_back({output, where});
                } else if constexpr (CoversLevels<Output>::value) {
                    if (!o.nested) {
                        resolved.push_back({output, where, OutputGrid(output, base, surface)});
                        return;
                    }
                    const std::string at = Child(where, "nested");
                    if (o.region) {
                        throw ConfigError(Child(where, "region"), "cannot be given with nested");
                    }
                    const std::size_t r = region(o.nested->region, Child(at, "region"));
                    const std::size_t count = RegionLevelCount(config.regions[r]);
                    std::vector<std::size_t> levels;
                    if (o.nested->level) {
                        if (*o.nested->level > count) {
                            throw ConfigError(Child(at, "level"),
                                              "region " + o.nested->region + " has " +
                                                  std::to_string(count) + " levels");
                        }
                        levels.push_back(*o.nested->level);
                    } else {
                        if (!Holds(o.path, kLevelKey)) {
                            throw ConfigError(Child(where, "path"),
                                              "needs {level} to cover every level");
                        }
                        for (std::size_t l = 0; l <= count; ++l) {
                            levels.push_back(l);
                        }
                    }
                    const RegionLayout::Group& group = layout.groups[layout.group[r]];
                    for (std::size_t level : levels) {
                        Output copy = o;
                        copy.nested->level = level;
                        copy.path = Replace(Replace(o.path, kRegionKey, o.nested->region),
                                            kLevelKey, std::to_string(level));
                        const std::size_t index = layout.GridIndex(r, level);
                        const NestedGrid& grid = group.grids[index];
                        if constexpr (std::is_same_v<Output, TerrainMeshOutput>) {
                            for (std::size_t child : grid.children) {
                                if (group.grids[child].region == layout.member[r]) {
                                    copy.cutouts.push_back(CentreBox(group.grids[child].grid));
                                }
                            }
                            const Vec2d site = Centre(group.base);
                            copy.siteX = o.siteX.value_or(site.x);
                            copy.siteY = o.siteY.value_or(site.y);
                        }
                        ResolvedOutput entry{copy, where, grid.grid};
                        if (level > 0) {
                            entry.region = r;
                        }
                        entry.group = layout.group[r];
                        entry.grids = {index};
                        resolved.push_back(std::move(entry));
                    }
                } else {
                    resolved.push_back({output, where, OutputGrid(output, base, surface)});
                }
            },
            output);
    }
    return resolved;
}

bool SplitsNestedMesh(const NestedMeshOutput& output) {
    return Holds(output.path, kLevelKey);
}

std::string NestedMeshFile(const NestedMeshOutput& output, const RegionLayout& layout,
                           std::size_t group, std::size_t grid) {
    const RegionLayout::Group& owner = layout.groups.at(group);
    const NestedGrid& nested = owner.grids.at(grid);
    const std::string name =
        nested.region ? layout.names[owner.regions[*nested.region]] : output.regions.front();
    return Replace(Replace(output.path, kRegionKey, name), kLevelKey, std::to_string(nested.level));
}

std::string RegionManifest(const ManifestOutput& manifest, const RegionLayout& layout,
                           const std::vector<ResolvedOutput>& outputs, const Surface& surface) {
    std::map<std::pair<std::size_t, std::size_t>, std::map<std::string, std::vector<std::string>>>
        files;
    for (const ResolvedOutput& output : outputs) {
        if (!output.group || std::holds_alternative<ManifestOutput>(output.spec)) {
            continue;
        }
        const std::string type = OutputType(output.spec);
        for (std::size_t grid : output.grids) {
            const auto* mesh = std::get_if<NestedMeshOutput>(&output.spec);
            const std::string path =
                mesh && SplitsNestedMesh(*mesh)
                    ? NestedMeshFile(*mesh, layout, *output.group, grid)
                    : std::visit([](const auto& o) { return o.path; }, output.spec);
            files[{*output.group, grid}][type].push_back(path);
        }
    }
    std::vector<bool> listed(layout.names.size(), manifest.regions.empty());
    for (const std::string& name : manifest.regions) {
        listed[*layout.Find(name)] = true;
    }
    nlohmann::ordered_json grids = nlohmann::ordered_json::array();
    for (std::size_t g = 0; g < layout.groups.size(); ++g) {
        const RegionLayout::Group& group = layout.groups[g];
        if (std::none_of(group.regions.begin(), group.regions.end(),
                         [&](std::size_t r) { return listed[r]; })) {
            continue;
        }
        for (std::size_t index = 0; index < group.grids.size(); ++index) {
            const NestedGrid& grid = group.grids[index];
            const std::optional<std::size_t> region =
                grid.region ? std::optional<std::size_t>(group.regions[*grid.region])
                            : std::nullopt;
            if (region && !listed[*region]) {
                continue;
            }
            const Bounds2d bounds = grid.grid.Bounds();
            nlohmann::ordered_json entry;
            entry["group"] = g;
            entry["index"] = index;
            entry["region"] = region ? nlohmann::ordered_json(layout.names[*region]) : nullptr;
            entry["level"] = grid.level;
            entry["parent"] = grid.parent ? nlohmann::ordered_json(*grid.parent) : nullptr;
            entry["width"] = grid.grid.width;
            entry["height"] = grid.grid.height;
            entry["pixel_size"] = std::fabs(grid.grid.transform.pixelHeight) * surface.UnitLength();
            entry["centre_x"] = 0.5 * (bounds.minX + bounds.maxX);
            entry["centre_y"] = 0.5 * (bounds.minY + bounds.maxY);
            entry["bounds"] = {bounds.minX, bounds.minY, bounds.maxX, bounds.maxY};
            nlohmann::ordered_json written = nlohmann::ordered_json::object();
            const auto found = files.find({g, index});
            if (found != files.end()) {
                for (const auto& [type, paths] : found->second) {
                    written[type] = paths;
                }
            }
            entry["files"] = written;
            grids.push_back(entry);
        }
    }
    nlohmann::ordered_json document;
    document["grids"] = grids;
    return document.dump(2) + "\n";
}

}  // namespace terraingen::config
