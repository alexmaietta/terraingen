// SPDX-License-Identifier: BSD-3-Clause

#include "config/albedo.h"

#include "terraingen/albedo/blast_zones.h"
#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/rock_albedo.h"
#include "terraingen/albedo/slope_albedo.h"
#include "terraingen/albedo/splotches.h"
#include "util/overloaded.h"

#include <utility>

namespace terraingen {

namespace {

/// Each layer of `stack` with its Hurst exponent applied, appended to `layers`.
template <typename Layers>
void AppendEffective(const std::vector<AlbedoFractalSpec>& stack, Layers& layers) {
    for (const AlbedoFractalSpec& layer : stack) {
        layers.push_back(layer.Effective());
    }
}

}  // namespace

SyntheticAlbedoOptions SyntheticAlbedoSpec::Options() const {
    SyntheticAlbedoOptions built = options;
    AppendEffective(hueNoiseDegrees, built.hueNoiseDegrees);
    AppendEffective(saturationNoise, built.saturationNoise);
    AppendEffective(valueNoise, built.valueNoise);
    return built;
}

namespace config {

namespace {

/// The top-level crater and boulder layers albedo layers can read.
struct AlbedoChecks {
    std::size_t craterLayers = 0;
    std::size_t boulderLayers = 0;
};

void CheckAlbedoSource(const SyntheticAlbedoSpec& spec, const std::filesystem::path& /*directory*/,
                       const std::string& path) {
    CheckNoiseStack(spec.hueNoiseDegrees, FieldPath(path, "hue_noise_deg"));
    CheckNoiseStack(spec.saturationNoise, FieldPath(path, "saturation_noise"));
    CheckNoiseStack(spec.valueNoise, FieldPath(path, "value_noise"));
    At(path, [&] { ValidateSyntheticAlbedoOptions(spec.Options()); });
}

void CheckAlbedoSource(const RasterAlbedoSpec& spec, const std::filesystem::path& directory,
                       const std::string& path) {
    const std::string file = FieldPath(path, "path");
    CheckNonEmpty(spec.path, file);
    const GeoTiffInfo info =
        AtFile(file, [&] { return ReadGeoTiffInfo(Resolve(spec.path, directory)); });
    for (const std::size_t band : spec.bands) {
        CheckBand(band, info, FieldPath(path, "bands"));
    }
    if (info.grid.transform.IsRotated()) {
        throw ConfigError(file, "rotated albedo maps are not supported");
    }
    At(FieldPath(path, "detail"), [&] { ValidateAlbedoDetail(spec.options.detail); });
}

void CheckAlbedoLayer(const CraterAlbedoSpec& craters, const std::string& path,
                      const AlbedoChecks& checks) {
    if (checks.craterLayers == 0) {
        throw ConfigError(path, "crater albedo needs a crater layer");
    }
    if (craters.craterLayer && *craters.craterLayer >= checks.craterLayers) {
        throw ConfigError(FieldPath(path, "crater_layer"),
                          "there are " + std::to_string(checks.craterLayers) + " crater layers");
    }
    At(path, [&] { ValidateCraterAlbedoOptions(craters.options); });
}

void CheckAlbedoLayer(const RockAlbedoSpec& rocks, const std::string& path,
                      const AlbedoChecks& checks) {
    if (checks.boulderLayers == 0) {
        throw ConfigError(path, "rock albedo needs a top-level boulder layer");
    }
    if (rocks.boulderLayer && *rocks.boulderLayer >= checks.boulderLayers) {
        throw ConfigError(FieldPath(path, "boulder_layer"),
                          "there are " + std::to_string(checks.boulderLayers) + " boulder layers");
    }
    At(path, [&] { ValidateRockAlbedoOptions(rocks.options); });
}

void CheckAlbedoLayer(const SplotchAlbedoOptions& splotches, const std::string& path,
                      const AlbedoChecks&) {
    At(path, [&] { ValidateSplotchAlbedoOptions(splotches); });
}

void CheckAlbedoLayer(const BlastZoneAlbedoSpec& zones, const std::string& path,
                      const AlbedoChecks&) {
    for (std::size_t z = 0; z < zones.zones.size(); ++z) {
        At(ItemPath(FieldPath(path, "zones"), z), [&] { ValidateBlastZone(zones.zones[z]); });
    }
}

void CheckAlbedoLayer(const SlopeAlbedoOptions& slopes, const std::string& path,
                      const AlbedoChecks&) {
    At(path, [&] { ValidateSlopeAlbedoOptions(slopes); });
}

/// The bands of a raster albedo map, scaled as its metadata or the spec says.
std::vector<Raster<float>> LoadAlbedoBands(const RasterAlbedoSpec& spec,
                                           const std::filesystem::path& directory) {
    const std::filesystem::path path = Resolve(spec.path, directory);
    const GeoTiffInfo info = ReadGeoTiffInfo(path);
    std::vector<Raster<float>> bands;
    for (const std::size_t band : spec.bands) {
        Raster<float> raster = ReadGeoTiff(path, band).raster;
        const BandScaling scaling{spec.scale.value_or(info.bandScaling[band].scale),
                                  spec.offset.value_or(info.bandScaling[band].offset)};
        if (!scaling.Identity()) {
            for (float& value : raster.Values()) {
                if (!raster.IsNoData(value)) {
                    value = static_cast<float>(static_cast<double>(value) * scaling.scale +
                                               scaling.offset);
                }
            }
        }
        bands.push_back(std::move(raster));
    }
    return bands;
}

std::shared_ptr<const AlbedoSource> BuildAlbedoSource(const AlbedoSourceSpec& spec,
                                                      const std::filesystem::path& directory) {
    using Result = std::shared_ptr<const AlbedoSource>;
    return std::visit(Overloaded{
                          [&](const SyntheticAlbedoSpec& s) -> Result {
                              return std::make_shared<SyntheticAlbedo>(s.Options());
                          },
                          [&](const RasterAlbedoSpec& s) -> Result {
                              return std::make_shared<RasterAlbedo>(LoadAlbedoBands(s, directory),
                                                                    s.options);
                          },
                      },
                      spec);
}

/// What albedo layers are built from: the terrain, and the layers it was built from.
struct AlbedoInputs {
    const std::vector<LayerSpec>& layers;
    const Terrain& terrain;
};

/// The terrain's layer `index`, which the configuration built as a `T`.
/// @throws ConfigError if the terrain has no `T` there, as when it was changed after building.
template <typename T>
std::shared_ptr<const T> TerrainLayer(const AlbedoInputs& inputs, std::size_t index) {
    const std::vector<LayerPtr>& layers = inputs.terrain.Layers();
    std::shared_ptr<const T> layer =
        index < layers.size() ? std::dynamic_pointer_cast<const T>(layers[index]) : nullptr;
    if (!layer) {
        throw ConfigError(ItemPath("layers", index),
                          "the terrain's layer here is not the one the configuration builds; "
                          "pass the terrain BuildTerrain built from this configuration");
    }
    return layer;
}

void AddAlbedoLayer(const CraterAlbedoSpec& craters, const AlbedoInputs& inputs, Albedo& albedo) {
    const std::vector<std::size_t> indices = LayerIndices<CraterLayerSpec>(inputs.layers);
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (!craters.craterLayer || *craters.craterLayer == i) {
            albedo.layers.push_back(std::make_shared<CraterAlbedo>(
                TerrainLayer<CraterLayer>(inputs, indices[i]), craters.options));
        }
    }
}

void AddAlbedoLayer(const RockAlbedoSpec& rocks, const AlbedoInputs& inputs, Albedo& albedo) {
    std::vector<RockSource> sources;
    for (const std::size_t i : LayerIndices<BoulderLayerSpec>(inputs.layers)) {
        sources.push_back({TerrainLayer<BoulderLayer>(inputs, i),
                           std::make_shared<const Terrain>(inputs.terrain.Beneath(i))});
    }
    if (rocks.boulderLayer) {
        sources = {sources[*rocks.boulderLayer]};
    }
    albedo.layers.push_back(std::make_shared<RockAlbedo>(rocks.options, std::move(sources)));
}

void AddAlbedoLayer(const SplotchAlbedoOptions& splotches, const AlbedoInputs&, Albedo& albedo) {
    albedo.layers.push_back(std::make_shared<SplotchAlbedo>(splotches));
}

void AddAlbedoLayer(const BlastZoneAlbedoSpec& zones, const AlbedoInputs&, Albedo& albedo) {
    albedo.layers.push_back(std::make_shared<BlastZoneAlbedo>(zones.zones));
}

void AddAlbedoLayer(const SlopeAlbedoOptions& slopes, const AlbedoInputs&, Albedo& albedo) {
    albedo.layers.push_back(std::make_shared<SlopeAlbedo>(slopes));
}

}  // namespace

void CheckAlbedo(const AlbedoSpec& albedo, const std::vector<LayerSpec>& layers,
                 const std::filesystem::path& directory) {
    std::visit([&](const auto& source) { CheckAlbedoSource(source, directory, "albedo.source"); },
               albedo.source);
    const AlbedoChecks checks{CountLayers<CraterLayerSpec>(layers),
                              CountLayers<BoulderLayerSpec>(layers)};
    for (std::size_t i = 0; i < albedo.layers.size(); ++i) {
        const std::string path = ItemPath("albedo.layers", i);
        std::visit([&](const auto& layer) { CheckAlbedoLayer(layer, path, checks); },
                   albedo.layers[i]);
    }
}

Albedo BuildValidAlbedo(const AlbedoSpec& spec, const std::vector<LayerSpec>& layers,
                        const Terrain& terrain, const std::filesystem::path& directory) {
    Albedo albedo;
    albedo.surface = terrain.Surface();
    albedo.source = BuildAlbedoSource(spec.source, directory);
    const AlbedoInputs inputs{layers, terrain};
    for (const AlbedoLayerSpec& layer : spec.layers) {
        std::visit([&](const auto& s) { AddAlbedoLayer(s, inputs, albedo); }, layer);
    }
    return albedo;
}

}  // namespace config

}  // namespace terraingen
