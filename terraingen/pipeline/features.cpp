/// @file
/// @brief Listing the craters and boulders of the built-in feature layers, as they apply them.
#include "terraingen/pipeline/features.h"

#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/secondaries.h"
#include "terraingen/layers/region.h"

#include <algorithm>
#include <optional>

namespace terraingen {

namespace {

/// The craters `layer` applies over `grid` with a context spacing of `spacing`, as its
/// `ApplySamples` would see them.
CraterFeatures CratersOf(const Layer& layer, const RasterGrid& grid, double spacing,
                         const Surface& surface) {
    const double resolved = std::max(spacing, grid.Spacing(surface));
    CraterFeatures placed;
    const auto add = [&](const CraterList& craters, const auto& attributesOf) {
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            placed.attributes.push_back(attributesOf(craters.At(n)));
        }
        placed.craters.Append(craters);
    };
    if (const auto* craters = dynamic_cast<const CraterLayer*>(&layer)) {
        add(craters->Craters(grid.Bounds(), resolved),
            [&](const Crater& crater) { return craters->Attributes(crater); });
    } else if (const auto* secondaries = dynamic_cast<const SecondaryCraterLayer*>(&layer)) {
        add(secondaries->Secondaries(grid.Bounds(), resolved),
            [&](const Crater& crater) { return secondaries->Attributes(crater); });
    } else if (const auto* region = dynamic_cast<const RegionLayer*>(&layer)) {
        const std::optional<RasterGrid> inner = region->InnerGrid(grid);
        if (!inner) {
            return placed;
        }
        const CraterFeatures all = CratersOf(region->Inner(), *inner, spacing, surface);
        for (std::size_t n = 0; n < all.craters.Size(); ++n) {
            const Crater crater = all.craters.At(n);
            if (region->Holds(crater.x, crater.y)) {
                placed.craters.Append(crater);
                placed.attributes.push_back(all.attributes[n]);
            }
        }
    }
    return placed;
}

/// The boulders `layer` places over `grid` on `ground`, as its `ApplySamples` would see them.
BoulderFeatures BouldersOf(const Layer& layer, const RasterGrid& grid, double spacing,
                           const Surface& surface, const Ground& ground) {
    const double resolved = std::max(spacing, grid.Spacing(surface));
    if (const auto* boulders = dynamic_cast<const BoulderLayer*>(&layer)) {
        BoulderFeatures placed{boulders->Boulders(grid.Bounds(), resolved, &ground), {}};
        placed.models = boulders->Models(placed.boulders);
        return placed;
    }
    BoulderFeatures placed;
    if (const auto* region = dynamic_cast<const RegionLayer*>(&layer)) {
        const std::optional<RasterGrid> inner = region->InnerGrid(grid);
        if (!inner) {
            return placed;
        }
        const BoulderFeatures all = BouldersOf(region->Inner(), *inner, spacing, surface, ground);
        std::copy_if(all.boulders.begin(), all.boulders.end(), std::back_inserter(placed.boulders),
                     [&](const Boulder& b) { return region->Holds(b.x, b.y); });
        std::copy_if(
            all.models.begin(), all.models.end(), std::back_inserter(placed.models),
            [&](const BoulderModel& m) { return region->Holds(m.boulder.x, m.boulder.y); });
    }
    return placed;
}

}  // namespace

CraterFeatures PlacedCraters(const std::vector<std::shared_ptr<const Layer>>& layers,
                             const RasterGrid& grid, double spacing, const Surface& surface) {
    const double resolved = std::max(spacing, grid.Spacing(surface));
    CraterFeatures placed;
    for (const auto& layer : layers) {
        const CraterFeatures found = CratersOf(*layer, grid, resolved, surface);
        placed.craters.Append(found.craters);
        placed.attributes.insert(placed.attributes.end(), found.attributes.begin(),
                                 found.attributes.end());
    }
    return placed;
}

CraterFeatures PlacedCraters(const Terrain& terrain, const RasterGrid& grid, double spacing) {
    return PlacedCraters(terrain.layers, grid, spacing, terrain.surface);
}

BoulderFeatures PlacedBoulders(const Terrain& terrain, const RasterGrid& grid, double spacing) {
    const double resolved = std::max(spacing, grid.Spacing(terrain.surface));
    BoulderFeatures placed;
    for (std::size_t n = 0; n < terrain.layers.size(); ++n) {
        const TerrainGround ground(terrain, n);
        const BoulderFeatures found =
            BouldersOf(*terrain.layers[n], grid, resolved, terrain.surface, ground);
        placed.boulders.insert(placed.boulders.end(), found.boulders.begin(), found.boulders.end());
        placed.models.insert(placed.models.end(), found.models.begin(), found.models.end());
    }
    return placed;
}

}  // namespace terraingen
