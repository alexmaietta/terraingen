#include "settings.h"

#include <cmath>
#include <exception>
#include <stdexcept>

namespace terraingen::viewer {

NoiseLayer LayerSettings::EffectiveLayer() const {
    NoiseLayer effective = layer;
    if (useHurst) {
        effective.fractal.gain = GainFromHurst(hurst, layer.fractal.lacunarity);
    }
    return effective;
}

LayerSettings DefaultLayer() {
    LayerSettings settings;
    settings.layer.fractal.seed = 1u;
    settings.layer.fractal.wavelength = 500.0f;
    settings.layer.fractal.amplitude = 50.0f;
    return settings;
}

SyntheticTerrainParams TerrainSettings::EffectiveTerrain() const {
    SyntheticTerrainParams params;
    params.layers.clear();
    for (const LayerSettings& layer : layers) {
        params.layers.push_back(layer.EffectiveLayer());
    }
    return params;
}

std::string ValidateSettings(const TerrainSettings& settings) {
    if (settings.size < 2) {
        return "Grid size must be at least 2";
    }
    if (!(std::isfinite(settings.pixelSize) && settings.pixelSize > 0.0)) {
        return "Pixel size must be positive";
    }
    for (std::size_t i = 0; i < settings.layers.size(); ++i) {
        if (settings.layers[i].useHurst && !std::isfinite(settings.layers[i].hurst)) {
            return "Layer " + std::to_string(i + 1) + ": Hurst exponent must be finite";
        }
    }
    try {
        ValidateSyntheticTerrain(settings.EffectiveTerrain());
    } catch (const std::invalid_argument& error) {
        return error.what();
    }
    return {};
}

Raster<float> GenerateTerrain(const TerrainSettings& settings) {
    const std::string error = ValidateSettings(settings);
    if (!error.empty()) {
        throw std::invalid_argument(error);
    }
    return GenerateSyntheticTerrain(settings.size, settings.size, settings.pixelSize,
                                    settings.EffectiveTerrain());
}

glm::vec3 SunDirection(float azimuthDegrees, float elevationDegrees) {
    const float azimuth = glm::radians(azimuthDegrees);
    const float elevation = glm::radians(elevationDegrees);
    return {std::sin(azimuth) * std::cos(elevation), std::cos(azimuth) * std::cos(elevation),
            std::sin(elevation)};
}

render::SurfaceSettings SurfaceViewSettings::Effective(const render::BrdfRegistry& registry) const {
    render::SurfaceSettings effective = surface;
    if (const render::BrdfModel* model = registry.Find(surface.brdf)) {
        const auto edited = brdfParams.find(surface.brdf);
        effective.brdfParams = edited != brdfParams.end()
                                   ? render::ResolveBrdfParams(*model, edited->second)
                                   : model->Defaults();
    }
    return effective;
}

std::variant<render::Image, std::string> LoadDetailTexture(const std::string& path,
                                                           bool normalMap) {
    if (path.empty()) {
        return std::string("No file given");
    }
    try {
        return render::LoadImage(path, normalMap ? render::ImageEncoding::Linear
                                                 : render::ImageEncoding::Srgb);
    } catch (const std::exception& error) {
        return std::string(error.what());
    }
}

}  // namespace terraingen::viewer
