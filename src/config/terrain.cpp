// SPDX-License-Identifier: BSD-3-Clause

#include "config/terrain.h"

#include "terraingen/config/build.h"

#include <stdexcept>
#include <utility>

namespace terraingen {

TerrainGenConfig::TerrainGenConfig() : TerrainGenConfig(ConfigSpec{}) {}

TerrainGenConfig::TerrainGenConfig(ConfigSpec spec)
    : spec_(std::make_shared<const ConfigSpec>(std::move(spec))) {}

const std::filesystem::path& TerrainGenConfig::Directory() const {
    return spec_->directory;
}

const ConfigSpec& TerrainGenConfig::Spec() const {
    return *spec_;
}

namespace config {

namespace {

/// The source `config` gives, else a "shape_model" source on a shape-model body; none on other
/// bodies, which then start from the default synthetic source.
std::optional<SourceSpec> EffectiveSource(const ConfigSpec& config) {
    if (config.source) {
        return config.source;
    }
    if (std::holds_alternative<ShapeModelSurfaceSpec>(config.body)) {
        return SourceSpec{ShapeModelSourceSpec{}};
    }
    return std::nullopt;
}

/// The terrain's source, the configuration's or the default synthetic noise unless the caller
/// gives one, and what the layers are built on.
LayerSources BuildLayerSources(const ConfigSpec& config, const std::filesystem::path& directory,
                               const std::shared_ptr<const Source>& external,
                               const Surface& surface, ConfigSummary& summary) {
    const std::optional<SourceSpec> spec = EffectiveSource(config);
    LayerSources sources;
    sources.posts = spec && HasPosts(*spec);
    if (external) {
        sources.terrain = external;
        sources.given = external;
        sources.measured = external;
        return sources;
    }
    sources.terrain =
        BuildSource(spec.value_or(SourceSpec{SyntheticSourceSpec{}}), directory, "source", surface);
    summary.source = BoundedGrid(*sources.terrain);
    if (spec) {
        sources.given = sources.terrain;
        sources.measured = HasMeasuredData(*spec) ? sources.terrain : nullptr;
    }
    return sources;
}

}  // namespace

CheckedTerrain BuildCheckedTerrain(const ConfigSpec& config, const std::filesystem::path& directory,
                                   const std::shared_ptr<const Source>& external) {
    CheckConfigRanges(config);
    ConfigSummary summary;
    Surface surface = BuildSurface(config.body, directory);
    const LayerSources sources = BuildLayerSources(config, directory, external, surface, summary);
    const OutputNeeds needs{config.albedo.has_value(), config.materials.has_value()};
    std::vector<OutputGrid> outputs = CheckOutputs(config.outputs, needs, surface, summary.source);
    for (const OutputGrid& output : outputs) {
        summary.outputGrids.push_back(output.grid);
    }
    BuiltLayers built = BuildLayers(config.layers, outputs, sources, directory, surface);
    summary.expectedCraters = built.expectedCraters;
    CheckedTerrain checked{Terrain(std::move(surface), sources.terrain, std::move(built.layers)),
                           std::move(summary), std::move(outputs)};
    if (config.albedo) {
        CheckAlbedo(*config.albedo, config.layers, directory);
    }
    if (config.materials) {
        CheckMaterials(*config.materials, HasOutput<PhotometryOutput>(config.outputs),
                       checked.terrain);
    }
    return checked;
}

}  // namespace config

ConfigSummary ValidateConfig(const TerrainGenConfig& value, const std::filesystem::path& given) {
    const ConfigSpec& config = value.Spec();
    return config::BuildCheckedTerrain(config, ConfigDirectory(config, given)).summary;
}

Surface BuildSurface(const TerrainGenConfig& value, const std::filesystem::path& given) {
    const ConfigSpec& config = value.Spec();
    return BuildSurface(config.body, ConfigDirectory(config, given));
}

Terrain BuildTerrain(const TerrainGenConfig& value, const std::filesystem::path& given) {
    const ConfigSpec& config = value.Spec();
    return config::BuildCheckedTerrain(config, ConfigDirectory(config, given)).terrain;
}

Terrain BuildTerrain(const TerrainGenConfig& value, std::shared_ptr<const Source> source,
                     const std::filesystem::path& given) {
    const ConfigSpec& config = value.Spec();
    if (!source) {
        throw std::invalid_argument("BuildTerrain needs a source");
    }
    if (config.source) {
        throw ConfigError("source", "must be left out when the caller gives the source");
    }
    return config::BuildCheckedTerrain(config, ConfigDirectory(config, given), source).terrain;
}

Albedo BuildAlbedo(const TerrainGenConfig& value, const Terrain& terrain,
                   const std::filesystem::path& given) {
    const ConfigSpec& config = value.Spec();
    const std::filesystem::path directory = ConfigDirectory(config, given);
    if (!config.albedo) {
        throw ConfigError("albedo", "the configuration has no albedo");
    }
    config::CheckConfigRanges(*config.albedo, "albedo");
    config::CheckAlbedo(*config.albedo, config.layers, directory);
    return config::BuildValidAlbedo(*config.albedo, config.layers, terrain, directory);
}

}  // namespace terraingen
