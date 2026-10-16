#include "terraingen/app/gui/terrain_scene.h"

#include <ernie/scene/schema.h>
#include <ernie/scene/tokens.h>
#include <ernie/util/tensor.h>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraingen::gui {

namespace {

namespace tokens = ernie::tokens;

/// The north-west corner and cell size of a north-up raster with square cells.
struct Placement {
    glm::dvec2 corner;
    double cellSize = 1.0;
};

Placement PlacementOf(const Raster<float>& raster, const char* what) {
    const GeoTransform& t = raster.Transform();
    if (raster.Width() < 2 || raster.Height() < 2) {
        throw std::invalid_argument(std::string(what) + " needs at least 2 x 2 cells");
    }
    if (t.rowRotation != 0.0 || t.colRotation != 0.0 || !(t.pixelWidth > 0.0) ||
        !(t.pixelHeight < 0.0) || std::abs(t.pixelWidth + t.pixelHeight) > 1e-9 * t.pixelWidth) {
        throw std::invalid_argument(std::string(what) +
                                    " must be north-up with square cells to view");
    }
    return {{t.originX, t.originY}, t.pixelWidth};
}

ernie::Tensor TensorOf(const Raster<float>& raster) {
    return ernie::Tensor::Copy<float>(raster.Values(), {static_cast<std::int64_t>(raster.Height()),
                                                        static_cast<std::int64_t>(raster.Width())});
}

}  // namespace

void SetUpScene(ernie::Scene& scene) {
    if (!scene.Has(kSunPrim)) {
        scene.Define(kSunPrim, tokens::kDistantLight)
            .Set(tokens::kTransform, ernie::DistantLightTransform({0.61, 0.61, 0.5}));
    }
    if (!scene.Has(kMaterialPrim)) {
        scene.Define(kMaterialPrim, tokens::kMaterial).Set(tokens::kAlbedo, glm::vec3(0.5f));
    }
    if (!scene.Has(kSettingsPrim)) {
        scene.Define(kSettingsPrim, tokens::kRenderSettings)
            .Set(tokens::kColorMode, tokens::kShadedColormap);
    }
}

void ShowTerrain(ernie::Scene& scene, const Raster<float>& heights, bool geographic,
                 const Raster<float>* albedo) {
    if (geographic) {
        throw std::invalid_argument("Heights in longitude and latitude cannot be viewed yet; "
                                    "view a projected raster");
    }
    const Placement place = PlacementOf(heights, "The heights");
    ernie::Prim terrain = scene.Define(kTerrainPrim, tokens::kTerrain);
    terrain.Write(tokens::kHeights, TensorOf(heights))
        .Set(tokens::kOrigin, place.corner)
        .Set(tokens::kCellSize, place.cellSize)
        .Set(tokens::kMaterialBinding, kMaterialPrim);
    if (heights.NoData()) {
        terrain.Set(tokens::kNoData, static_cast<double>(*heights.NoData()));
    } else {
        scene.Clear(kTerrainPrim, tokens::kNoData);
    }

    ernie::Prim material = scene.Define(kMaterialPrim, tokens::kMaterial);
    if (albedo == nullptr || albedo->Empty()) {
        scene.Clear(kMaterialPrim, tokens::kAlbedoMap);
        return;
    }
    const Placement map = PlacementOf(*albedo, "The albedo");
    if (albedo->Width() != heights.Width() || albedo->Height() != heights.Height() ||
        map.corner != place.corner || map.cellSize != place.cellSize) {
        throw std::invalid_argument("The albedo must lie on the heights' grid");
    }
    material.Write(tokens::kAlbedoMap, TensorOf(*albedo))
        .Set(tokens::kAlbedoMapOrigin, map.corner)
        .Set(tokens::kAlbedoMapCellSize, map.cellSize);
}

}  // namespace terraingen::gui
